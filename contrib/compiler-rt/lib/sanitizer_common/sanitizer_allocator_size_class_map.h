//===-- sanitizer_allocator_size_class_map.h --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Part of the Sanitizer Allocator.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_ALLOCATOR_H
#error This file must be included inside sanitizer_allocator.h
#endif

// SizeClassMap maps allocation sizes into size classes and back.
// Class 0 always corresponds to size 0.
// The other sizes are controlled by the template parameters:
//   kMinSizeLog: defines the class 1    as 2^kMinSizeLog.
//   kMaxSizeLog: defines the last class as 2^kMaxSizeLog.
//   kMidSizeLog: the classes starting from 1 increase with step
//                2^kMinSizeLog until 2^kMidSizeLog.
//   kNumBits: the number of non-zero bits in sizes after 2^kMidSizeLog.
//             E.g. with kNumBits==3 all size classes after 2^kMidSizeLog
//             look like 0b1xx0..0, where x is either 0 or 1.
//
// Example: kNumBits=3, kMidSizeLog=4, kMidSizeLog=8, kMaxSizeLog=17:
//
// Classes 1 - 16 correspond to sizes 16 to 256 (size = class_id * 16).
// Next 4 classes: 256 + i * 64  (i = 1 to 4).
// Next 4 classes: 512 + i * 128 (i = 1 to 4).
// ...
// Next 4 classes: 2^k + i * 2^(k-2) (i = 1 to 4).
// Last class corresponds to kMaxSize = 1 << kMaxSizeLog.
//
// This structure of the size class map gives us:
//   - Efficient table-free class-to-size and size-to-class functions.
//   - Difference between two consequent size classes is between 14% and 25%
//
// This class also gives a hint to a thread-caching allocator about the amount
// of chunks that need to be cached per-thread:
//  - kMaxNumCachedHint is a hint for maximal number of chunks per size class.
//    The actual number is computed in TransferBatch.
//  - (1 << kMaxBytesCachedLog) is the maximal number of bytes per size class.
//
// Part of output of SizeClassMap::Print():
// c00 => s: 0 diff: +0 00% l 0 cached: 0 0; id 0
// c01 => s: 16 diff: +16 00% l 4 cached: 256 4096; id 1
// c02 => s: 32 diff: +16 100% l 5 cached: 256 8192; id 2
// c03 => s: 48 diff: +16 50% l 5 cached: 256 12288; id 3
// c04 => s: 64 diff: +16 33% l 6 cached: 256 16384; id 4
// c05 => s: 80 diff: +16 25% l 6 cached: 256 20480; id 5
// c06 => s: 96 diff: +16 20% l 6 cached: 256 24576; id 6
// c07 => s: 112 diff: +16 16% l 6 cached: 256 28672; id 7
//
// c08 => s: 128 diff: +16 14% l 7 cached: 256 32768; id 8
// c09 => s: 144 diff: +16 12% l 7 cached: 256 36864; id 9
// c10 => s: 160 diff: +16 11% l 7 cached: 256 40960; id 10
// c11 => s: 176 diff: +16 10% l 7 cached: 256 45056; id 11
// c12 => s: 192 diff: +16 09% l 7 cached: 256 49152; id 12
// c13 => s: 208 diff: +16 08% l 7 cached: 256 53248; id 13
// c14 => s: 224 diff: +16 07% l 7 cached: 256 57344; id 14
// c15 => s: 240 diff: +16 07% l 7 cached: 256 61440; id 15
//
// c16 => s: 256 diff: +16 06% l 8 cached: 256 65536; id 16
// c17 => s: 320 diff: +64 25% l 8 cached: 204 65280; id 17
// c18 => s: 384 diff: +64 20% l 8 cached: 170 65280; id 18
// c19 => s: 448 diff: +64 16% l 8 cached: 146 65408; id 19
//
// c20 => s: 512 diff: +64 14% l 9 cached: 128 65536; id 20
// c21 => s: 640 diff: +128 25% l 9 cached: 102 65280; id 21
// c22 => s: 768 diff: +128 20% l 9 cached: 85 65280; id 22
// c23 => s: 896 diff: +128 16% l 9 cached: 73 65408; id 23
//
// c24 => s: 1024 diff: +128 14% l 10 cached: 64 65536; id 24
// c25 => s: 1280 diff: +256 25% l 10 cached: 51 65280; id 25
// c26 => s: 1536 diff: +256 20% l 10 cached: 42 64512; id 26
// c27 => s: 1792 diff: +256 16% l 10 cached: 36 64512; id 27
//
// ...
//
// c48 => s: 65536 diff: +8192 14% l 16 cached: 1 65536; id 48
// c49 => s: 81920 diff: +16384 25% l 16 cached: 1 81920; id 49
// c50 => s: 98304 diff: +16384 20% l 16 cached: 1 98304; id 50
// c51 => s: 114688 diff: +16384 16% l 16 cached: 1 114688; id 51
//
// c52 => s: 131072 diff: +16384 14% l 17 cached: 1 131072; id 52
//
//
// Another example (kNumBits=2):
// c00 => s: 0 diff: +0 00% l 0 cached: 0 0; id 0
// c01 => s: 32 diff: +32 00% l 5 cached: 64 2048; id 1
// c02 => s: 64 diff: +32 100% l 6 cached: 64 4096; id 2
// c03 => s: 96 diff: +32 50% l 6 cached: 64 6144; id 3
// c04 => s: 128 diff: +32 33% l 7 cached: 64 8192; id 4
// c05 => s: 160 diff: +32 25% l 7 cached: 64 10240; id 5
// c06 => s: 192 diff: +32 20% l 7 cached: 64 12288; id 6
// c07 => s: 224 diff: +32 16% l 7 cached: 64 14336; id 7
// c08 => s: 256 diff: +32 14% l 8 cached: 64 16384; id 8
// c09 => s: 384 diff: +128 50% l 8 cached: 42 16128; id 9
// c10 => s: 512 diff: +128 33% l 9 cached: 32 16384; id 10
// c11 => s: 768 diff: +256 50% l 9 cached: 21 16128; id 11
// c12 => s: 1024 diff: +256 33% l 10 cached: 16 16384; id 12
// c13 => s: 1536 diff: +512 50% l 10 cached: 10 15360; id 13
// c14 => s: 2048 diff: +512 33% l 11 cached: 8 16384; id 14
// c15 => s: 3072 diff: +1024 50% l 11 cached: 5 15360; id 15
// c16 => s: 4096 diff: +1024 33% l 12 cached: 4 16384; id 16
// c17 => s: 6144 diff: +2048 50% l 12 cached: 2 12288; id 17
// c18 => s: 8192 diff: +2048 33% l 13 cached: 2 16384; id 18
// c19 => s: 12288 diff: +4096 50% l 13 cached: 1 12288; id 19
// c20 => s: 16384 diff: +4096 33% l 14 cached: 1 16384; id 20
// c21 => s: 24576 diff: +8192 50% l 14 cached: 1 24576; id 21
// c22 => s: 32768 diff: +8192 33% l 15 cached: 1 32768; id 22
// c23 => s: 49152 diff: +16384 50% l 15 cached: 1 49152; id 23
// c24 => s: 65536 diff: +16384 33% l 16 cached: 1 65536; id 24
// c25 => s: 98304 diff: +32768 50% l 16 cached: 1 98304; id 25
// c26 => s: 131072 diff: +32768 33% l 17 cached: 1 131072; id 26

template <uptr kNumBits, uptr kMinSizeLog, uptr kMidSizeLog, uptr kMaxSizeLog,
          uptr kMaxNumCachedHintT, uptr kMaxBytesCachedLog>
class SizeClassMap {
  static const uptr kMinSize = 1 << kMinSizeLog;
  static const uptr kMidSize = 1 << kMidSizeLog;
  static const uptr kMidClass = kMidSize / kMinSize;
  static const uptr S = kNumBits - 1;
  static const uptr M = (1 << S) - 1;

 public:
  // kMaxNumCachedHintT is a power of two. It serves as a hint
  // for the size of TransferBatch, the actual size could be a bit smaller.
  static const uptr kMaxNumCachedHint = kMaxNumCachedHintT;
  COMPILER_CHECK((kMaxNumCachedHint & (kMaxNumCachedHint - 1)) == 0);

  static const uptr kMaxSize = 1UL << kMaxSizeLog;
  static const uptr kNumClasses =
      kMidClass + ((kMaxSizeLog - kMidSizeLog) << S) + 1 + 1;
  static const uptr kLargestClassID = kNumClasses - 2;
  static const uptr kBatchClassID = kNumClasses - 1;
  COMPILER_CHECK(kNumClasses >= 16 && kNumClasses <= 256);
  static const uptr kNumClassesRounded =
      kNumClasses <= 32  ? 32 :
      kNumClasses <= 64  ? 64 :
      kNumClasses <= 128 ? 128 : 256;

  static uptr Size(uptr class_id) {
    // Estimate the result for kBatchClassID because this class does not know
    // the exact size of TransferBatch. It's OK since we are using the actual
    // sizeof(TransferBatch) where it matters.
    if (UNLIKELY(class_id == kBatchClassID))
      return kMaxNumCachedHint * sizeof(uptr);
    if (class_id <= kMidClass)
      return kMinSize * class_id;
    class_id -= kMidClass;
    uptr t = kMidSize << (class_id >> S);
    return t + (t >> S) * (class_id & M);
  }

  static uptr ClassID(uptr size) {
    if (UNLIKELY(size > kMaxSize))
      return 0;
    if (size <= kMidSize)
      return (size + kMinSize - 1) >> kMinSizeLog;
    const uptr l = MostSignificantSetBitIndex(size);
    const uptr hbits = (size >> (l - S)) & M;
    const uptr lbits = size & ((1U << (l - S)) - 1);
    const uptr l1 = l - kMidSizeLog;
    return kMidClass + (l1 << S) + hbits + (lbits > 0);
  }

  static uptr MaxCachedHint(uptr size) {
    DCHECK_LE(size, kMaxSize);
    if (UNLIKELY(size == 0))
      return 0;
    uptr n;
    // Force a 32-bit division if the template parameters allow for it.
    if (kMaxBytesCachedLog > 31 || kMaxSizeLog > 31)
      n = (1UL << kMaxBytesCachedLog) / size;
    else
      n = (1U << kMaxBytesCachedLog) / static_cast<u32>(size);
    return Max<uptr>(1U, Min(kMaxNumCachedHint, n));
  }

  static void Print() {
    uptr prev_s = 0;
    uptr total_cached = 0;
    for (uptr i = 0; i < kNumClasses; i++) {
      uptr s = Size(i);
      if (s >= kMidSize / 2 && (s & (s - 1)) == 0)
        Printf("\n");
      uptr d = s - prev_s;
      uptr p = prev_s ? (d * 100 / prev_s) : 0;
      uptr l = s ? MostSignificantSetBitIndex(s) : 0;
      uptr cached = MaxCachedHint(s) * s;
      if (i == kBatchClassID)
        d = p = l = 0;
      Printf("c%02zd => s: %zd diff: +%zd %02zd%% l %zd "
             "cached: %zd %zd; id %zd\n",
             i, Size(i), d, p, l, MaxCachedHint(s), cached, ClassID(s));
      total_cached += cached;
      prev_s = s;
    }
    Printf("Total cached: %zd\n", total_cached);
  }

  static void Validate() {
    for (uptr c = 1; c < kNumClasses; c++) {
      // Printf("Validate: c%zd\n", c);
      uptr s = Size(c);
      CHECK_NE(s, 0U);
      if (c == kBatchClassID)
        continue;
      CHECK_EQ(ClassID(s), c);
      if (c < kLargestClassID)
        CHECK_EQ(ClassID(s + 1), c + 1);
      CHECK_EQ(ClassID(s - 1), c);
      CHECK_GT(Size(c), Size(c - 1));
    }
    CHECK_EQ(ClassID(kMaxSize + 1), 0);

    for (uptr s = 1; s <= kMaxSize; s++) {
      uptr c = ClassID(s);
      // Printf("s%zd => c%zd\n", s, c);
      CHECK_LT(c, kNumClasses);
      CHECK_GE(Size(c), s);
      if (c > 0)
        CHECK_LT(Size(c - 1), s);
    }
  }
};

typedef SizeClassMap<3, 4, 8, 17, 128, 16> DefaultSizeClassMap;
typedef SizeClassMap<3, 4, 8, 17, 64, 14> CompactSizeClassMap;
typedef SizeClassMap<2, 5, 9, 16, 64, 14> VeryCompactSizeClassMap;

// The following SizeClassMap only holds a way small number of cached entries,
// allowing for denser per-class arrays, smaller memory footprint and usually
// better performances in threaded environments.
typedef SizeClassMap<3, 4, 8, 17, 8, 10> DenseSizeClassMap;
// Similar to VeryCompact map above, this one has a small number of different
// size classes, and also reduced thread-local caches.
typedef SizeClassMap<2, 5, 9, 16, 8, 10> VeryDenseSizeClassMap;
