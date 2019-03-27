/*===- InstrProfiling.h- Support library for PGO instrumentation ----------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/

#ifndef PROFILE_INSTRPROFILING_INTERNALH_
#define PROFILE_INSTRPROFILING_INTERNALH_

#include <stddef.h>

#include "InstrProfiling.h"

/*!
 * \brief Write instrumentation data to the given buffer, given explicit
 * pointers to the live data in memory.  This function is probably not what you
 * want.  Use __llvm_profile_get_size_for_buffer instead.  Use this function if
 * your program has a custom memory layout.
 */
uint64_t __llvm_profile_get_size_for_buffer_internal(
    const __llvm_profile_data *DataBegin, const __llvm_profile_data *DataEnd,
    const uint64_t *CountersBegin, const uint64_t *CountersEnd,
    const char *NamesBegin, const char *NamesEnd);

/*!
 * \brief Write instrumentation data to the given buffer, given explicit
 * pointers to the live data in memory.  This function is probably not what you
 * want.  Use __llvm_profile_write_buffer instead.  Use this function if your
 * program has a custom memory layout.
 *
 * \pre \c Buffer is the start of a buffer at least as big as \a
 * __llvm_profile_get_size_for_buffer_internal().
 */
int __llvm_profile_write_buffer_internal(
    char *Buffer, const __llvm_profile_data *DataBegin,
    const __llvm_profile_data *DataEnd, const uint64_t *CountersBegin,
    const uint64_t *CountersEnd, const char *NamesBegin, const char *NamesEnd);

/*!
 * The data structure describing the data to be written by the
 * low level writer callback function.
 */
typedef struct ProfDataIOVec {
  const void *Data;
  size_t ElmSize;
  size_t NumElm;
} ProfDataIOVec;

struct ProfDataWriter;
typedef uint32_t (*WriterCallback)(struct ProfDataWriter *This, ProfDataIOVec *,
                                   uint32_t NumIOVecs);

typedef struct ProfDataWriter {
  WriterCallback Write;
  void *WriterCtx;
} ProfDataWriter;

/*!
 * The data structure for buffered IO of profile data.
 */
typedef struct ProfBufferIO {
  ProfDataWriter *FileWriter;
  uint32_t OwnFileWriter;
  /* The start of the buffer. */
  uint8_t *BufferStart;
  /* Total size of the buffer. */
  uint32_t BufferSz;
  /* Current byte offset from the start of the buffer. */
  uint32_t CurOffset;
} ProfBufferIO;

/* The creator interface used by testing.  */
ProfBufferIO *lprofCreateBufferIOInternal(void *File, uint32_t BufferSz);

/*!
 * This is the interface to create a handle for buffered IO.
 */
ProfBufferIO *lprofCreateBufferIO(ProfDataWriter *FileWriter);

/*!
 * The interface to destroy the bufferIO handle and reclaim
 * the memory.
 */
void lprofDeleteBufferIO(ProfBufferIO *BufferIO);

/*!
 * This is the interface to write \c Data of \c Size bytes through
 * \c BufferIO. Returns 0 if successful, otherwise return -1.
 */
int lprofBufferIOWrite(ProfBufferIO *BufferIO, const uint8_t *Data,
                       uint32_t Size);
/*!
 * The interface to flush the remaining data in the buffer.
 * through the low level writer callback.
 */
int lprofBufferIOFlush(ProfBufferIO *BufferIO);

/* The low level interface to write data into a buffer. It is used as the
 * callback by other high level writer methods such as buffered IO writer
 * and profile data writer.  */
uint32_t lprofBufferWriter(ProfDataWriter *This, ProfDataIOVec *IOVecs,
                           uint32_t NumIOVecs);
void initBufferWriter(ProfDataWriter *BufferWriter, char *Buffer);

struct ValueProfData;
struct ValueProfRecord;
struct InstrProfValueData;
struct ValueProfNode;

/*!
 * The class that defines a set of methods to read value profile
 * data for streaming/serialization from the instrumentation runtime.
 */
typedef struct VPDataReaderType {
  uint32_t (*InitRTRecord)(const __llvm_profile_data *Data,
                           uint8_t *SiteCountArray[]);
  /* Function pointer to getValueProfRecordHeader method. */
  uint32_t (*GetValueProfRecordHeaderSize)(uint32_t NumSites);
  /* Function pointer to getFristValueProfRecord method. */  
  struct ValueProfRecord *(*GetFirstValueProfRecord)(struct ValueProfData *);
  /* Return the number of value data for site \p Site.  */
  uint32_t (*GetNumValueDataForSite)(uint32_t VK, uint32_t Site);
  /* Return the total size of the value profile data of the 
   * current function.  */
  uint32_t (*GetValueProfDataSize)(void);
  /*! 
   * Read the next \p N value data for site \p Site and store the data
   * in \p Dst. \p StartNode is the first value node to start with if
   * it is not null. The function returns the pointer to the value
   * node pointer to be used as the \p StartNode of the next batch reading.
   * If there is nothing left, it returns NULL.
   */
  struct ValueProfNode *(*GetValueData)(uint32_t ValueKind, uint32_t Site,
                                        struct InstrProfValueData *Dst,
                                        struct ValueProfNode *StartNode,
                                        uint32_t N);
} VPDataReaderType;

/* Write profile data to destinitation. If SkipNameDataWrite is set to 1,
   the name data is already in destintation, we just skip over it. */
int lprofWriteData(ProfDataWriter *Writer, VPDataReaderType *VPDataReader,
                   int SkipNameDataWrite);
int lprofWriteDataImpl(ProfDataWriter *Writer,
                       const __llvm_profile_data *DataBegin,
                       const __llvm_profile_data *DataEnd,
                       const uint64_t *CountersBegin,
                       const uint64_t *CountersEnd,
                       VPDataReaderType *VPDataReader, const char *NamesBegin,
                       const char *NamesEnd, int SkipNameDataWrite);

/* Merge value profile data pointed to by SrcValueProfData into
 * in-memory profile counters pointed by to DstData.  */
void lprofMergeValueProfData(struct ValueProfData *SrcValueProfData,
                             __llvm_profile_data *DstData);

VPDataReaderType *lprofGetVPDataReader();

/* Internal interface used by test to reset the max number of 
 * tracked values per value site to be \p MaxVals.
 */
void lprofSetMaxValsPerSite(uint32_t MaxVals);
void lprofSetupValueProfiler();

/* Return the profile header 'signature' value associated with the current
 * executable or shared library. The signature value can be used to for
 * a profile name that is unique to this load module so that it does not
 * collide with profiles from other binaries. It also allows shared libraries
 * to dump merged profile data into its own profile file. */
uint64_t lprofGetLoadModuleSignature();

/* 
 * Return non zero value if the profile data has already been
 * dumped to the file.
 */
unsigned lprofProfileDumped();
void lprofSetProfileDumped();

COMPILER_RT_VISIBILITY extern void (*FreeHook)(void *);
COMPILER_RT_VISIBILITY extern uint8_t *DynamicBufferIOBuffer;
COMPILER_RT_VISIBILITY extern uint32_t VPBufferSize;
COMPILER_RT_VISIBILITY extern uint32_t VPMaxNumValsPerSite;
/* Pointer to the start of static value counters to be allocted. */
COMPILER_RT_VISIBILITY extern ValueProfNode *CurrentVNode;
COMPILER_RT_VISIBILITY extern ValueProfNode *EndVNode;
extern void (*VPMergeHook)(struct ValueProfData *, __llvm_profile_data *);

#endif
