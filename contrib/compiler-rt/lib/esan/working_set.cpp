//===-- working_set.cpp ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// This file contains working-set-specific code.
//===----------------------------------------------------------------------===//

#include "working_set.h"
#include "esan.h"
#include "esan_circular_buffer.h"
#include "esan_flags.h"
#include "esan_shadow.h"
#include "esan_sideline.h"
#include "sanitizer_common/sanitizer_procmaps.h"

// We shadow every cache line of app memory with one shadow byte.
// - The highest bit of each shadow byte indicates whether the corresponding
//   cache line has ever been accessed.
// - The lowest bit of each shadow byte indicates whether the corresponding
//   cache line was accessed since the last sample.
// - The other bits are used for working set snapshots at successively
//   lower frequencies, each bit to the left from the lowest bit stepping
//   down the frequency by 2 to the power of getFlags()->snapshot_step.
// Thus we have something like this:
//   Bit 0: Since last sample
//   Bit 1: Since last 2^2 samples
//   Bit 2: Since last 2^4 samples
//   Bit 3: ...
//   Bit 7: Ever accessed.
// We live with races in accessing each shadow byte.
typedef unsigned char byte;

namespace __esan {

// Our shadow memory assumes that the line size is 64.
static const u32 CacheLineSize = 64;

// See the shadow byte layout description above.
static const u32 TotalWorkingSetBitIdx = 7;
// We accumulate to the left until we hit this bit.
// We don't need to accumulate to the final bit as it's set on each ref
// by the compiler instrumentation.
static const u32 MaxAccumBitIdx = 6;
static const u32 CurWorkingSetBitIdx = 0;
static const byte ShadowAccessedVal =
  (1 << TotalWorkingSetBitIdx) | (1 << CurWorkingSetBitIdx);

static SidelineThread Thread;
// If we use real-time-based timer samples this won't overflow in any realistic
// scenario, but if we switch to some other unit (such as memory accesses) we
// may want to consider a 64-bit int.
static u32 SnapshotNum;

// We store the wset size for each of 8 different sampling frequencies.
static const u32 NumFreq = 8; // One for each bit of our shadow bytes.
// We cannot use static objects as the global destructor is called
// prior to our finalize routine.
// These are each circular buffers, sized up front.
CircularBuffer<u32> SizePerFreq[NumFreq];
// We cannot rely on static initializers (they may run too late) but
// we record the size here for clarity:
u32 CircularBufferSizes[NumFreq] = {
  // These are each mmap-ed so our minimum is one page.
  32*1024,
  16*1024,
  8*1024,
  4*1024,
  4*1024,
  4*1024,
  4*1024,
  4*1024,
};

void processRangeAccessWorkingSet(uptr PC, uptr Addr, SIZE_T Size,
                                  bool IsWrite) {
  if (Size == 0)
    return;
  SIZE_T I = 0;
  uptr LineSize = getFlags()->cache_line_size;
  // As Addr+Size could overflow at the top of a 32-bit address space,
  // we avoid the simpler formula that rounds the start and end.
  SIZE_T NumLines = Size / LineSize +
    // Add any extra at the start or end adding on an extra line:
    (LineSize - 1 + Addr % LineSize + Size % LineSize) / LineSize;
  byte *Shadow = (byte *)appToShadow(Addr);
  // Write shadow bytes until we're word-aligned.
  while (I < NumLines && (uptr)Shadow % 4 != 0) {
    if ((*Shadow & ShadowAccessedVal) != ShadowAccessedVal)
      *Shadow |= ShadowAccessedVal;
    ++Shadow;
    ++I;
  }
  // Write whole shadow words at a time.
  // Using a word-stride loop improves the runtime of a microbenchmark of
  // memset calls by 10%.
  u32 WordValue = ShadowAccessedVal | ShadowAccessedVal << 8 |
    ShadowAccessedVal << 16 | ShadowAccessedVal << 24;
  while (I + 4 <= NumLines) {
    if ((*(u32*)Shadow & WordValue) != WordValue)
      *(u32*)Shadow |= WordValue;
    Shadow += 4;
    I += 4;
  }
  // Write any trailing shadow bytes.
  while (I < NumLines) {
    if ((*Shadow & ShadowAccessedVal) != ShadowAccessedVal)
      *Shadow |= ShadowAccessedVal;
    ++Shadow;
    ++I;
  }
}

// This routine will word-align ShadowStart and ShadowEnd prior to scanning.
// It does *not* clear for BitIdx==TotalWorkingSetBitIdx, as that top bit
// measures the access during the entire execution and should never be cleared.
static u32 countAndClearShadowValues(u32 BitIdx, uptr ShadowStart,
                                     uptr ShadowEnd) {
  u32 WorkingSetSize = 0;
  u32 ByteValue = 0x1 << BitIdx;
  u32 WordValue = ByteValue | ByteValue << 8 | ByteValue << 16 |
    ByteValue << 24;
  // Get word aligned start.
  ShadowStart = RoundDownTo(ShadowStart, sizeof(u32));
  bool Accum = getFlags()->record_snapshots && BitIdx < MaxAccumBitIdx;
  // Do not clear the bit that measures access during the entire execution.
  bool Clear = BitIdx < TotalWorkingSetBitIdx;
  for (u32 *Ptr = (u32 *)ShadowStart; Ptr < (u32 *)ShadowEnd; ++Ptr) {
    if ((*Ptr & WordValue) != 0) {
      byte *BytePtr = (byte *)Ptr;
      for (u32 j = 0; j < sizeof(u32); ++j) {
        if (BytePtr[j] & ByteValue) {
          ++WorkingSetSize;
          if (Accum) {
            // Accumulate to the lower-frequency bit to the left.
            BytePtr[j] |= (ByteValue << 1);
          }
        }
      }
      if (Clear) {
        // Clear this bit from every shadow byte.
        *Ptr &= ~WordValue;
      }
    }
  }
  return WorkingSetSize;
}

// Scan shadow memory to calculate the number of cache lines being accessed,
// i.e., the number of non-zero bits indexed by BitIdx in each shadow byte.
// We also clear the lowest bits (most recent working set snapshot).
// We do *not* clear for BitIdx==TotalWorkingSetBitIdx, as that top bit
// measures the access during the entire execution and should never be cleared.
static u32 computeWorkingSizeAndReset(u32 BitIdx) {
  u32 WorkingSetSize = 0;
  MemoryMappingLayout MemIter(true/*cache*/);
  MemoryMappedSegment Segment;
  while (MemIter.Next(&Segment)) {
    VPrintf(4, "%s: considering %p-%p app=%d shadow=%d prot=%u\n", __FUNCTION__,
            Segment.start, Segment.end, Segment.protection,
            isAppMem(Segment.start), isShadowMem(Segment.start));
    if (isShadowMem(Segment.start) && Segment.IsWritable()) {
      VPrintf(3, "%s: walking %p-%p\n", __FUNCTION__, Segment.start,
              Segment.end);
      WorkingSetSize +=
          countAndClearShadowValues(BitIdx, Segment.start, Segment.end);
    }
  }
  return WorkingSetSize;
}

// This is invoked from a signal handler but in a sideline thread doing nothing
// else so it is a little less fragile than a typical signal handler.
static void takeSample(void *Arg) {
  u32 BitIdx = CurWorkingSetBitIdx;
  u32 Freq = 1;
  ++SnapshotNum; // Simpler to skip 0 whose mod matches everything.
  while (BitIdx <= MaxAccumBitIdx && (SnapshotNum % Freq) == 0) {
    u32 NumLines = computeWorkingSizeAndReset(BitIdx);
    VReport(1, "%s: snapshot #%5d bit %d freq %4d: %8u\n", SanitizerToolName,
            SnapshotNum, BitIdx, Freq, NumLines);
    SizePerFreq[BitIdx].push_back(NumLines);
    Freq = Freq << getFlags()->snapshot_step;
    BitIdx++;
  }
}

unsigned int getSampleCountWorkingSet()
{
  return SnapshotNum;
}

// Initialization that must be done before any instrumented code is executed.
void initializeShadowWorkingSet() {
  CHECK(getFlags()->cache_line_size == CacheLineSize);
  registerMemoryFaultHandler();
}

void initializeWorkingSet() {
  if (getFlags()->record_snapshots) {
    for (u32 i = 0; i < NumFreq; ++i)
      SizePerFreq[i].initialize(CircularBufferSizes[i]);
    Thread.launchThread(takeSample, nullptr, getFlags()->sample_freq);
  }
}

static u32 getPeriodForPrinting(u32 MilliSec, const char *&Unit) {
  if (MilliSec > 600000) {
    Unit = "min";
    return MilliSec / 60000;
  } else if (MilliSec > 10000) {
    Unit = "sec";
    return MilliSec / 1000;
  } else {
    Unit = "ms";
    return MilliSec;
  }
}

static u32 getSizeForPrinting(u32 NumOfCachelines, const char *&Unit) {
  // We need a constant to avoid software divide support:
  static const u32 KilobyteCachelines = (0x1 << 10) / CacheLineSize;
  static const u32 MegabyteCachelines = KilobyteCachelines << 10;

  if (NumOfCachelines > 10 * MegabyteCachelines) {
    Unit = "MB";
    return NumOfCachelines / MegabyteCachelines;
  } else if (NumOfCachelines > 10 * KilobyteCachelines) {
    Unit = "KB";
    return NumOfCachelines / KilobyteCachelines;
  } else {
    Unit = "Bytes";
    return NumOfCachelines * CacheLineSize;
  }
}

void reportWorkingSet() {
  const char *Unit;
  if (getFlags()->record_snapshots) {
    u32 Freq = 1;
    Report(" Total number of samples: %u\n", SnapshotNum);
    for (u32 i = 0; i < NumFreq; ++i) {
      u32 Time = getPeriodForPrinting(getFlags()->sample_freq*Freq, Unit);
      Report(" Samples array #%d at period %u %s\n", i, Time, Unit);
      // FIXME: report whether we wrapped around and thus whether we
      // have data on the whole run or just the last N samples.
      for (u32 j = 0; j < SizePerFreq[i].size(); ++j) {
        u32 Size = getSizeForPrinting(SizePerFreq[i][j], Unit);
        Report("#%4d: %8u %s (%9u cache lines)\n", j, Size, Unit,
               SizePerFreq[i][j]);
      }
      Freq = Freq << getFlags()->snapshot_step;
    }
  }

  // Get the working set size for the entire execution.
  u32 NumOfCachelines = computeWorkingSizeAndReset(TotalWorkingSetBitIdx);
  u32 Size = getSizeForPrinting(NumOfCachelines, Unit);
  Report(" %s: the total working set size: %u %s (%u cache lines)\n",
         SanitizerToolName, Size, Unit, NumOfCachelines);
}

int finalizeWorkingSet() {
  if (getFlags()->record_snapshots)
    Thread.joinThread();
  reportWorkingSet();
  if (getFlags()->record_snapshots) {
    for (u32 i = 0; i < NumFreq; ++i)
      SizePerFreq[i].free();
  }
  return 0;
}

} // namespace __esan
