/*===-- InstrProfData.inc - instr profiling runtime structures -*- C++ -*-=== *\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/
/*
 * This is the master file that defines all the data structure, signature,
 * constant literals that are shared across profiling runtime library,
 * compiler (instrumentation), and host tools (reader/writer). The entities
 * defined in this file affect the profile runtime ABI, the raw profile format,
 * or both.
 *
 * The file has two identical copies. The master copy lives in LLVM and
 * the other one  sits in compiler-rt/lib/profile directory. To make changes
 * in this file, first modify the master copy and copy it over to compiler-rt.
 * Testing of any change in this file can start only after the two copies are
 * synced up.
 *
 * The first part of the file includes macros that defines types, names, and
 * initializers for the member fields of the core data structures. The field
 * declarations for one structure is enabled by defining the field activation
 * macro associated with that structure. Only one field activation record
 * can be defined at one time and the rest definitions will be filtered out by
 * the preprocessor.
 *
 * Examples of how the template is used to instantiate structure definition:
 * 1. To declare a structure:
 *
 * struct ProfData {
 * #define INSTR_PROF_DATA(Type, LLVMType, Name, Initializer) \
 *    Type Name;
 * #include "llvm/ProfileData/InstrProfData.inc"
 * };
 *
 * 2. To construct LLVM type arrays for the struct type:
 *
 * Type *DataTypes[] = {
 * #define INSTR_PROF_DATA(Type, LLVMType, Name, Initializer) \
 *   LLVMType,
 * #include "llvm/ProfileData/InstrProfData.inc"
 * };
 *
 * 4. To construct constant array for the initializers:
 * #define INSTR_PROF_DATA(Type, LLVMType, Name, Initializer) \
 *   Initializer,
 * Constant *ConstantVals[] = {
 * #include "llvm/ProfileData/InstrProfData.inc"
 * };
 *
 *
 * The second part of the file includes definitions all other entities that
 * are related to runtime ABI and format. When no field activation macro is
 * defined, this file can be included to introduce the definitions.
 *
\*===----------------------------------------------------------------------===*/

/* Functions marked with INSTR_PROF_VISIBILITY must have hidden visibility in
 * the compiler runtime. */
#ifndef INSTR_PROF_VISIBILITY
#define INSTR_PROF_VISIBILITY
#endif

/* INSTR_PROF_DATA start. */
/* Definition of member fields of the per-function control structure. */
#ifndef INSTR_PROF_DATA
#define INSTR_PROF_DATA(Type, LLVMType, Name, Initializer)
#else
#define INSTR_PROF_DATA_DEFINED
#endif
INSTR_PROF_DATA(const uint64_t, llvm::Type::getInt64Ty(Ctx), NameRef, \
                ConstantInt::get(llvm::Type::getInt64Ty(Ctx), \
                IndexedInstrProf::ComputeHash(getPGOFuncNameVarInitializer(Inc->getName()))))
INSTR_PROF_DATA(const uint64_t, llvm::Type::getInt64Ty(Ctx), FuncHash, \
                ConstantInt::get(llvm::Type::getInt64Ty(Ctx), \
                Inc->getHash()->getZExtValue()))
INSTR_PROF_DATA(const IntPtrT, llvm::Type::getInt64PtrTy(Ctx), CounterPtr, \
                ConstantExpr::getBitCast(CounterPtr, \
                llvm::Type::getInt64PtrTy(Ctx)))
/* This is used to map function pointers for the indirect call targets to
 * function name hashes during the conversion from raw to merged profile
 * data.
 */
INSTR_PROF_DATA(const IntPtrT, llvm::Type::getInt8PtrTy(Ctx), FunctionPointer, \
                FunctionAddr)
INSTR_PROF_DATA(IntPtrT, llvm::Type::getInt8PtrTy(Ctx), Values, \
                ValuesPtrExpr)
INSTR_PROF_DATA(const uint32_t, llvm::Type::getInt32Ty(Ctx), NumCounters, \
                ConstantInt::get(llvm::Type::getInt32Ty(Ctx), NumCounters))
INSTR_PROF_DATA(const uint16_t, Int16ArrayTy, NumValueSites[IPVK_Last+1], \
                ConstantArray::get(Int16ArrayTy, Int16ArrayVals))
#undef INSTR_PROF_DATA
/* INSTR_PROF_DATA end. */


/* This is an internal data structure used by value profiler. It
 * is defined here to allow serialization code sharing by LLVM
 * to be used in unit test.
 *
 * typedef struct ValueProfNode {
 *   // InstrProfValueData VData;
 *   uint64_t Value;
 *   uint64_t Count;
 *   struct ValueProfNode *Next;
 * } ValueProfNode;
 */
/* INSTR_PROF_VALUE_NODE start. */
#ifndef INSTR_PROF_VALUE_NODE
#define INSTR_PROF_VALUE_NODE(Type, LLVMType, Name, Initializer)
#else
#define INSTR_PROF_DATA_DEFINED
#endif
INSTR_PROF_VALUE_NODE(uint64_t, llvm::Type::getInt64Ty(Ctx), Value, \
                      ConstantInt::get(llvm::Type::GetInt64Ty(Ctx), 0))
INSTR_PROF_VALUE_NODE(uint64_t, llvm::Type::getInt64Ty(Ctx), Count, \
                      ConstantInt::get(llvm::Type::GetInt64Ty(Ctx), 0))
INSTR_PROF_VALUE_NODE(PtrToNodeT, llvm::Type::getInt8PtrTy(Ctx), Next, \
                      ConstantInt::get(llvm::Type::GetInt8PtrTy(Ctx), 0))
#undef INSTR_PROF_VALUE_NODE
/* INSTR_PROF_VALUE_NODE end. */

/* INSTR_PROF_RAW_HEADER  start */
/* Definition of member fields of the raw profile header data structure. */
#ifndef INSTR_PROF_RAW_HEADER
#define INSTR_PROF_RAW_HEADER(Type, Name, Initializer)
#else
#define INSTR_PROF_DATA_DEFINED
#endif
INSTR_PROF_RAW_HEADER(uint64_t, Magic, __llvm_profile_get_magic())
INSTR_PROF_RAW_HEADER(uint64_t, Version, __llvm_profile_get_version())
INSTR_PROF_RAW_HEADER(uint64_t, DataSize, DataSize)
INSTR_PROF_RAW_HEADER(uint64_t, CountersSize, CountersSize)
INSTR_PROF_RAW_HEADER(uint64_t, NamesSize,  NamesSize)
INSTR_PROF_RAW_HEADER(uint64_t, CountersDelta, (uintptr_t)CountersBegin)
INSTR_PROF_RAW_HEADER(uint64_t, NamesDelta, (uintptr_t)NamesBegin)
INSTR_PROF_RAW_HEADER(uint64_t, ValueKindLast, IPVK_Last)
#undef INSTR_PROF_RAW_HEADER
/* INSTR_PROF_RAW_HEADER  end */

/* VALUE_PROF_FUNC_PARAM start */
/* Definition of parameter types of the runtime API used to do value profiling
 * for a given value site.
 */
#ifndef VALUE_PROF_FUNC_PARAM
#define VALUE_PROF_FUNC_PARAM(ArgType, ArgName, ArgLLVMType)
#define INSTR_PROF_COMMA
#else
#define INSTR_PROF_DATA_DEFINED
#define INSTR_PROF_COMMA ,
#endif
VALUE_PROF_FUNC_PARAM(uint64_t, TargetValue, Type::getInt64Ty(Ctx)) \
                      INSTR_PROF_COMMA
VALUE_PROF_FUNC_PARAM(void *, Data, Type::getInt8PtrTy(Ctx)) INSTR_PROF_COMMA
#ifndef VALUE_RANGE_PROF
VALUE_PROF_FUNC_PARAM(uint32_t, CounterIndex, Type::getInt32Ty(Ctx))
#else /* VALUE_RANGE_PROF */
VALUE_PROF_FUNC_PARAM(uint32_t, CounterIndex, Type::getInt32Ty(Ctx)) \
                      INSTR_PROF_COMMA
VALUE_PROF_FUNC_PARAM(uint64_t, PreciseRangeStart, Type::getInt64Ty(Ctx)) \
                      INSTR_PROF_COMMA
VALUE_PROF_FUNC_PARAM(uint64_t, PreciseRangeLast, Type::getInt64Ty(Ctx)) \
                      INSTR_PROF_COMMA
VALUE_PROF_FUNC_PARAM(uint64_t, LargeValue, Type::getInt64Ty(Ctx))
#endif /*VALUE_RANGE_PROF */
#undef VALUE_PROF_FUNC_PARAM
#undef INSTR_PROF_COMMA
/* VALUE_PROF_FUNC_PARAM end */

/* VALUE_PROF_KIND start */
#ifndef VALUE_PROF_KIND
#define VALUE_PROF_KIND(Enumerator, Value)
#else
#define INSTR_PROF_DATA_DEFINED
#endif
/* For indirect function call value profiling, the addresses of the target
 * functions are profiled by the instrumented code. The target addresses are
 * written in the raw profile data and converted to target function name's MD5
 * hash by the profile reader during deserialization.  Typically, this happens
 * when the raw profile data is read during profile merging.
 *
 * For this remapping the ProfData is used.  ProfData contains both the function
 * name hash and the function address.
 */
VALUE_PROF_KIND(IPVK_IndirectCallTarget, 0)
/* For memory intrinsic functions size profiling. */
VALUE_PROF_KIND(IPVK_MemOPSize, 1)
/* These two kinds must be the last to be
 * declared. This is to make sure the string
 * array created with the template can be
 * indexed with the kind value.
 */
VALUE_PROF_KIND(IPVK_First, IPVK_IndirectCallTarget)
VALUE_PROF_KIND(IPVK_Last, IPVK_MemOPSize)

#undef VALUE_PROF_KIND
/* VALUE_PROF_KIND end */

/* COVMAP_FUNC_RECORD start */
/* Definition of member fields of the function record structure in coverage
 * map.
 */
#ifndef COVMAP_FUNC_RECORD
#define COVMAP_FUNC_RECORD(Type, LLVMType, Name, Initializer)
#else
#define INSTR_PROF_DATA_DEFINED
#endif
#ifdef COVMAP_V1
COVMAP_FUNC_RECORD(const IntPtrT, llvm::Type::getInt8PtrTy(Ctx), \
                   NamePtr, llvm::ConstantExpr::getBitCast(NamePtr, \
                   llvm::Type::getInt8PtrTy(Ctx)))
COVMAP_FUNC_RECORD(const uint32_t, llvm::Type::getInt32Ty(Ctx), NameSize, \
                   llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), \
                   NameValue.size()))
#else
COVMAP_FUNC_RECORD(const int64_t, llvm::Type::getInt64Ty(Ctx), NameRef, \
                   llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), \
                   llvm::IndexedInstrProf::ComputeHash(NameValue)))
#endif
COVMAP_FUNC_RECORD(const uint32_t, llvm::Type::getInt32Ty(Ctx), DataSize, \
                   llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx),\
                   CoverageMapping.size()))
COVMAP_FUNC_RECORD(const uint64_t, llvm::Type::getInt64Ty(Ctx), FuncHash, \
                   llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), FuncHash))
#undef COVMAP_FUNC_RECORD
/* COVMAP_FUNC_RECORD end.  */

/* COVMAP_HEADER start */
/* Definition of member fields of coverage map header.
 */
#ifndef COVMAP_HEADER
#define COVMAP_HEADER(Type, LLVMType, Name, Initializer)
#else
#define INSTR_PROF_DATA_DEFINED
#endif
COVMAP_HEADER(uint32_t, Int32Ty, NRecords, \
              llvm::ConstantInt::get(Int32Ty,  FunctionRecords.size()))
COVMAP_HEADER(uint32_t, Int32Ty, FilenamesSize, \
              llvm::ConstantInt::get(Int32Ty, FilenamesSize))
COVMAP_HEADER(uint32_t, Int32Ty, CoverageSize, \
              llvm::ConstantInt::get(Int32Ty, CoverageMappingSize))
COVMAP_HEADER(uint32_t, Int32Ty, Version, \
              llvm::ConstantInt::get(Int32Ty, CovMapVersion::CurrentVersion))
#undef COVMAP_HEADER
/* COVMAP_HEADER end.  */


#ifdef INSTR_PROF_SECT_ENTRY
#define INSTR_PROF_DATA_DEFINED
INSTR_PROF_SECT_ENTRY(IPSK_data, \
                      INSTR_PROF_QUOTE(INSTR_PROF_DATA_COMMON), \
                      INSTR_PROF_QUOTE(INSTR_PROF_DATA_COFF), "__DATA,")
INSTR_PROF_SECT_ENTRY(IPSK_cnts, \
                      INSTR_PROF_QUOTE(INSTR_PROF_CNTS_COMMON), \
                      INSTR_PROF_QUOTE(INSTR_PROF_CNTS_COFF), "__DATA,")
INSTR_PROF_SECT_ENTRY(IPSK_name, \
                      INSTR_PROF_QUOTE(INSTR_PROF_NAME_COMMON), \
                      INSTR_PROF_QUOTE(INSTR_PROF_NAME_COFF), "__DATA,")
INSTR_PROF_SECT_ENTRY(IPSK_vals, \
                      INSTR_PROF_QUOTE(INSTR_PROF_VALS_COMMON), \
                      INSTR_PROF_QUOTE(INSTR_PROF_VALS_COFF), "__DATA,")
INSTR_PROF_SECT_ENTRY(IPSK_vnodes, \
                      INSTR_PROF_QUOTE(INSTR_PROF_VNODES_COMMON), \
                      INSTR_PROF_QUOTE(INSTR_PROF_VNODES_COFF), "__DATA,")
INSTR_PROF_SECT_ENTRY(IPSK_covmap, \
                      INSTR_PROF_QUOTE(INSTR_PROF_COVMAP_COMMON), \
                      INSTR_PROF_QUOTE(INSTR_PROF_COVMAP_COFF), "__LLVM_COV,")

#undef INSTR_PROF_SECT_ENTRY
#endif


#ifdef INSTR_PROF_VALUE_PROF_DATA
#define INSTR_PROF_DATA_DEFINED

#define INSTR_PROF_MAX_NUM_VAL_PER_SITE 255
/*!
 * This is the header of the data structure that defines the on-disk
 * layout of the value profile data of a particular kind for one function.
 */
typedef struct ValueProfRecord {
  /* The kind of the value profile record. */
  uint32_t Kind;
  /*
   * The number of value profile sites. It is guaranteed to be non-zero;
   * otherwise the record for this kind won't be emitted.
   */
  uint32_t NumValueSites;
  /*
   * The first element of the array that stores the number of profiled
   * values for each value site. The size of the array is NumValueSites.
   * Since NumValueSites is greater than zero, there is at least one
   * element in the array.
   */
  uint8_t SiteCountArray[1];

  /*
   * The fake declaration is for documentation purpose only.
   * Align the start of next field to be on 8 byte boundaries.
  uint8_t Padding[X];
   */

  /* The array of value profile data. The size of the array is the sum
   * of all elements in SiteCountArray[].
  InstrProfValueData ValueData[];
   */

#ifdef __cplusplus
  /*!
   * Return the number of value sites.
   */
  uint32_t getNumValueSites() const { return NumValueSites; }
  /*!
   * Read data from this record and save it to Record.
   */
  void deserializeTo(InstrProfRecord &Record,
                     InstrProfSymtab *SymTab);
  /*
   * In-place byte swap:
   * Do byte swap for this instance. \c Old is the original order before
   * the swap, and \c New is the New byte order.
   */
  void swapBytes(support::endianness Old, support::endianness New);
#endif
} ValueProfRecord;

/*!
 * Per-function header/control data structure for value profiling
 * data in indexed format.
 */
typedef struct ValueProfData {
  /*
   * Total size in bytes including this field. It must be a multiple
   * of sizeof(uint64_t).
   */
  uint32_t TotalSize;
  /*
   *The number of value profile kinds that has value profile data.
   * In this implementation, a value profile kind is considered to
   * have profile data if the number of value profile sites for the
   * kind is not zero. More aggressively, the implementation can
   * choose to check the actual data value: if none of the value sites
   * has any profiled values, the kind can be skipped.
   */
  uint32_t NumValueKinds;

  /*
   * Following are a sequence of variable length records. The prefix/header
   * of each record is defined by ValueProfRecord type. The number of
   * records is NumValueKinds.
   * ValueProfRecord Record_1;
   * ValueProfRecord Record_N;
   */

#if __cplusplus
  /*!
   * Return the total size in bytes of the on-disk value profile data
   * given the data stored in Record.
   */
  static uint32_t getSize(const InstrProfRecord &Record);
  /*!
   * Return a pointer to \c ValueProfData instance ready to be streamed.
   */
  static std::unique_ptr<ValueProfData>
  serializeFrom(const InstrProfRecord &Record);
  /*!
   * Check the integrity of the record.
   */
  Error checkIntegrity();
  /*!
   * Return a pointer to \c ValueProfileData instance ready to be read.
   * All data in the instance are properly byte swapped. The input
   * data is assumed to be in little endian order.
   */
  static Expected<std::unique_ptr<ValueProfData>>
  getValueProfData(const unsigned char *SrcBuffer,
                   const unsigned char *const SrcBufferEnd,
                   support::endianness SrcDataEndianness);
  /*!
   * Swap byte order from \c Endianness order to host byte order.
   */
  void swapBytesToHost(support::endianness Endianness);
  /*!
   * Swap byte order from host byte order to \c Endianness order.
   */
  void swapBytesFromHost(support::endianness Endianness);
  /*!
   * Return the total size of \c ValueProfileData.
   */
  uint32_t getSize() const { return TotalSize; }
  /*!
   * Read data from this data and save it to \c Record.
   */
  void deserializeTo(InstrProfRecord &Record,
                     InstrProfSymtab *SymTab);
  void operator delete(void *ptr) { ::operator delete(ptr); }
#endif
} ValueProfData;

/*
 * The closure is designed to abstact away two types of value profile data:
 * - InstrProfRecord which is the primary data structure used to
 *   represent profile data in host tools (reader, writer, and profile-use)
 * - value profile runtime data structure suitable to be used by C
 *   runtime library.
 *
 * Both sources of data need to serialize to disk/memory-buffer in common
 * format: ValueProfData. The abstraction allows compiler-rt's raw profiler
 * writer to share the same format and code with indexed profile writer.
 *
 * For documentation of the member methods below, refer to corresponding methods
 * in class InstrProfRecord.
 */
typedef struct ValueProfRecordClosure {
  const void *Record;
  uint32_t (*GetNumValueKinds)(const void *Record);
  uint32_t (*GetNumValueSites)(const void *Record, uint32_t VKind);
  uint32_t (*GetNumValueData)(const void *Record, uint32_t VKind);
  uint32_t (*GetNumValueDataForSite)(const void *R, uint32_t VK, uint32_t S);

  /*
   * After extracting the value profile data from the value profile record,
   * this method is used to map the in-memory value to on-disk value. If
   * the method is null, value will be written out untranslated.
   */
  uint64_t (*RemapValueData)(uint32_t, uint64_t Value);
  void (*GetValueForSite)(const void *R, InstrProfValueData *Dst, uint32_t K,
                          uint32_t S);
  ValueProfData *(*AllocValueProfData)(size_t TotalSizeInBytes);
} ValueProfRecordClosure;

INSTR_PROF_VISIBILITY ValueProfRecord *
getFirstValueProfRecord(ValueProfData *VPD);
INSTR_PROF_VISIBILITY ValueProfRecord *
getValueProfRecordNext(ValueProfRecord *VPR);
INSTR_PROF_VISIBILITY InstrProfValueData *
getValueProfRecordValueData(ValueProfRecord *VPR);
INSTR_PROF_VISIBILITY uint32_t
getValueProfRecordHeaderSize(uint32_t NumValueSites);

#undef INSTR_PROF_VALUE_PROF_DATA
#endif  /* INSTR_PROF_VALUE_PROF_DATA */


#ifdef INSTR_PROF_COMMON_API_IMPL
#define INSTR_PROF_DATA_DEFINED
#ifdef __cplusplus
#define INSTR_PROF_INLINE inline
#define INSTR_PROF_NULLPTR nullptr
#else
#define INSTR_PROF_INLINE
#define INSTR_PROF_NULLPTR NULL
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

/*!
 * Return the \c ValueProfRecord header size including the
 * padding bytes.
 */
INSTR_PROF_VISIBILITY INSTR_PROF_INLINE
uint32_t getValueProfRecordHeaderSize(uint32_t NumValueSites) {
  uint32_t Size = offsetof(ValueProfRecord, SiteCountArray) +
                  sizeof(uint8_t) * NumValueSites;
  /* Round the size to multiple of 8 bytes. */
  Size = (Size + 7) & ~7;
  return Size;
}

/*!
 * Return the total size of the value profile record including the
 * header and the value data.
 */
INSTR_PROF_VISIBILITY INSTR_PROF_INLINE
uint32_t getValueProfRecordSize(uint32_t NumValueSites,
                                uint32_t NumValueData) {
  return getValueProfRecordHeaderSize(NumValueSites) +
         sizeof(InstrProfValueData) * NumValueData;
}

/*!
 * Return the pointer to the start of value data array.
 */
INSTR_PROF_VISIBILITY INSTR_PROF_INLINE
InstrProfValueData *getValueProfRecordValueData(ValueProfRecord *This) {
  return (InstrProfValueData *)((char *)This + getValueProfRecordHeaderSize(
                                                   This->NumValueSites));
}

/*!
 * Return the total number of value data for \c This record.
 */
INSTR_PROF_VISIBILITY INSTR_PROF_INLINE
uint32_t getValueProfRecordNumValueData(ValueProfRecord *This) {
  uint32_t NumValueData = 0;
  uint32_t I;
  for (I = 0; I < This->NumValueSites; I++)
    NumValueData += This->SiteCountArray[I];
  return NumValueData;
}

/*!
 * Use this method to advance to the next \c This \c ValueProfRecord.
 */
INSTR_PROF_VISIBILITY INSTR_PROF_INLINE
ValueProfRecord *getValueProfRecordNext(ValueProfRecord *This) {
  uint32_t NumValueData = getValueProfRecordNumValueData(This);
  return (ValueProfRecord *)((char *)This +
                             getValueProfRecordSize(This->NumValueSites,
                                                    NumValueData));
}

/*!
 * Return the first \c ValueProfRecord instance.
 */
INSTR_PROF_VISIBILITY INSTR_PROF_INLINE
ValueProfRecord *getFirstValueProfRecord(ValueProfData *This) {
  return (ValueProfRecord *)((char *)This + sizeof(ValueProfData));
}

/* Closure based interfaces.  */

/*!
 * Return the total size in bytes of the on-disk value profile data
 * given the data stored in Record.
 */
INSTR_PROF_VISIBILITY uint32_t
getValueProfDataSize(ValueProfRecordClosure *Closure) {
  uint32_t Kind;
  uint32_t TotalSize = sizeof(ValueProfData);
  const void *Record = Closure->Record;

  for (Kind = IPVK_First; Kind <= IPVK_Last; Kind++) {
    uint32_t NumValueSites = Closure->GetNumValueSites(Record, Kind);
    if (!NumValueSites)
      continue;
    TotalSize += getValueProfRecordSize(NumValueSites,
                                        Closure->GetNumValueData(Record, Kind));
  }
  return TotalSize;
}

/*!
 * Extract value profile data of a function for the profile kind \c ValueKind
 * from the \c Closure and serialize the data into \c This record instance.
 */
INSTR_PROF_VISIBILITY void
serializeValueProfRecordFrom(ValueProfRecord *This,
                             ValueProfRecordClosure *Closure,
                             uint32_t ValueKind, uint32_t NumValueSites) {
  uint32_t S;
  const void *Record = Closure->Record;
  This->Kind = ValueKind;
  This->NumValueSites = NumValueSites;
  InstrProfValueData *DstVD = getValueProfRecordValueData(This);

  for (S = 0; S < NumValueSites; S++) {
    uint32_t ND = Closure->GetNumValueDataForSite(Record, ValueKind, S);
    This->SiteCountArray[S] = ND;
    Closure->GetValueForSite(Record, DstVD, ValueKind, S);
    DstVD += ND;
  }
}

/*!
 * Extract value profile data of a function  from the \c Closure
 * and serialize the data into \c DstData if it is not NULL or heap
 * memory allocated by the \c Closure's allocator method. If \c
 * DstData is not null, the caller is expected to set the TotalSize
 * in DstData.
 */
INSTR_PROF_VISIBILITY ValueProfData *
serializeValueProfDataFrom(ValueProfRecordClosure *Closure,
                           ValueProfData *DstData) {
  uint32_t Kind;
  uint32_t TotalSize =
      DstData ? DstData->TotalSize : getValueProfDataSize(Closure);

  ValueProfData *VPD =
      DstData ? DstData : Closure->AllocValueProfData(TotalSize);

  VPD->TotalSize = TotalSize;
  VPD->NumValueKinds = Closure->GetNumValueKinds(Closure->Record);
  ValueProfRecord *VR = getFirstValueProfRecord(VPD);
  for (Kind = IPVK_First; Kind <= IPVK_Last; Kind++) {
    uint32_t NumValueSites = Closure->GetNumValueSites(Closure->Record, Kind);
    if (!NumValueSites)
      continue;
    serializeValueProfRecordFrom(VR, Closure, Kind, NumValueSites);
    VR = getValueProfRecordNext(VR);
  }
  return VPD;
}

#undef INSTR_PROF_COMMON_API_IMPL
#endif /* INSTR_PROF_COMMON_API_IMPL */

/*============================================================================*/

#ifndef INSTR_PROF_DATA_DEFINED

#ifndef INSTR_PROF_DATA_INC
#define INSTR_PROF_DATA_INC

/* Helper macros.  */
#define INSTR_PROF_SIMPLE_QUOTE(x) #x
#define INSTR_PROF_QUOTE(x) INSTR_PROF_SIMPLE_QUOTE(x)
#define INSTR_PROF_SIMPLE_CONCAT(x,y) x ## y
#define INSTR_PROF_CONCAT(x,y) INSTR_PROF_SIMPLE_CONCAT(x,y)

/* Magic number to detect file format and endianness.
 * Use 255 at one end, since no UTF-8 file can use that character.  Avoid 0,
 * so that utilities, like strings, don't grab it as a string.  129 is also
 * invalid UTF-8, and high enough to be interesting.
 * Use "lprofr" in the centre to stand for "LLVM Profile Raw", or "lprofR"
 * for 32-bit platforms.
 */
#define INSTR_PROF_RAW_MAGIC_64 (uint64_t)255 << 56 | (uint64_t)'l' << 48 | \
       (uint64_t)'p' << 40 | (uint64_t)'r' << 32 | (uint64_t)'o' << 24 |  \
        (uint64_t)'f' << 16 | (uint64_t)'r' << 8 | (uint64_t)129
#define INSTR_PROF_RAW_MAGIC_32 (uint64_t)255 << 56 | (uint64_t)'l' << 48 | \
       (uint64_t)'p' << 40 | (uint64_t)'r' << 32 | (uint64_t)'o' << 24 |  \
        (uint64_t)'f' << 16 | (uint64_t)'R' << 8 | (uint64_t)129

/* Raw profile format version (start from 1). */
#define INSTR_PROF_RAW_VERSION 4
/* Indexed profile format version (start from 1). */
#define INSTR_PROF_INDEX_VERSION 5
/* Coverage mapping format vresion (start from 0). */
#define INSTR_PROF_COVMAP_VERSION 2

/* Profile version is always of type uint64_t. Reserve the upper 8 bits in the
 * version for other variants of profile. We set the lowest bit of the upper 8
 * bits (i.e. bit 56) to 1 to indicate if this is an IR-level instrumentaiton
 * generated profile, and 0 if this is a Clang FE generated profile.
 */
#define VARIANT_MASKS_ALL 0xff00000000000000ULL
#define GET_VERSION(V) ((V) & ~VARIANT_MASKS_ALL)
#define VARIANT_MASK_IR_PROF (0x1ULL << 56)
#define INSTR_PROF_RAW_VERSION_VAR __llvm_profile_raw_version
#define INSTR_PROF_PROFILE_RUNTIME_VAR __llvm_profile_runtime

/* The variable that holds the name of the profile data
 * specified via command line. */
#define INSTR_PROF_PROFILE_NAME_VAR __llvm_profile_filename

/* section name strings common to all targets other
   than WIN32 */
#define INSTR_PROF_DATA_COMMON __llvm_prf_data
#define INSTR_PROF_NAME_COMMON __llvm_prf_names
#define INSTR_PROF_CNTS_COMMON __llvm_prf_cnts
#define INSTR_PROF_VALS_COMMON __llvm_prf_vals
#define INSTR_PROF_VNODES_COMMON __llvm_prf_vnds
#define INSTR_PROF_COVMAP_COMMON __llvm_covmap
/* Win32 */
#define INSTR_PROF_DATA_COFF .lprfd
#define INSTR_PROF_NAME_COFF .lprfn
#define INSTR_PROF_CNTS_COFF .lprfc
#define INSTR_PROF_VALS_COFF .lprfv
#define INSTR_PROF_VNODES_COFF .lprfnd
#define INSTR_PROF_COVMAP_COFF .lcovmap

#ifdef _WIN32
/* Runtime section names and name strings.  */
#define INSTR_PROF_DATA_SECT_NAME INSTR_PROF_DATA_COFF
#define INSTR_PROF_NAME_SECT_NAME INSTR_PROF_NAME_COFF
#define INSTR_PROF_CNTS_SECT_NAME INSTR_PROF_CNTS_COFF
/* Array of pointers. Each pointer points to a list
 * of value nodes associated with one value site.
 */
#define INSTR_PROF_VALS_SECT_NAME INSTR_PROF_VALS_COFF
/* Value profile nodes section. */
#define INSTR_PROF_VNODES_SECT_NAME INSTR_PROF_VNODES_COFF
#define INSTR_PROF_COVMAP_SECT_NAME INSTR_PROF_COVMAP_COFF
#else
/* Runtime section names and name strings.  */
#define INSTR_PROF_DATA_SECT_NAME INSTR_PROF_DATA_COMMON
#define INSTR_PROF_NAME_SECT_NAME INSTR_PROF_NAME_COMMON
#define INSTR_PROF_CNTS_SECT_NAME INSTR_PROF_CNTS_COMMON
/* Array of pointers. Each pointer points to a list
 * of value nodes associated with one value site.
 */
#define INSTR_PROF_VALS_SECT_NAME INSTR_PROF_VALS_COMMON
/* Value profile nodes section. */
#define INSTR_PROF_VNODES_SECT_NAME INSTR_PROF_VNODES_COMMON
#define INSTR_PROF_COVMAP_SECT_NAME INSTR_PROF_COVMAP_COMMON
#endif

#define INSTR_PROF_DATA_SECT_NAME_STR                                          \
  INSTR_PROF_QUOTE(INSTR_PROF_DATA_SECT_NAME)
#define INSTR_PROF_NAME_SECT_NAME_STR                                          \
  INSTR_PROF_QUOTE(INSTR_PROF_NAME_SECT_NAME)
#define INSTR_PROF_CNTS_SECT_NAME_STR                                          \
  INSTR_PROF_QUOTE(INSTR_PROF_CNTS_SECT_NAME)
#define INSTR_PROF_COVMAP_SECT_NAME_STR                                        \
  INSTR_PROF_QUOTE(INSTR_PROF_COVMAP_SECT_NAME)
#define INSTR_PROF_VALS_SECT_NAME_STR                                          \
  INSTR_PROF_QUOTE(INSTR_PROF_VALS_SECT_NAME)
#define INSTR_PROF_VNODES_SECT_NAME_STR                                        \
  INSTR_PROF_QUOTE(INSTR_PROF_VNODES_SECT_NAME)

/* Macros to define start/stop section symbol for a given
 * section on Linux. For instance
 * INSTR_PROF_SECT_START(INSTR_PROF_DATA_SECT_NAME) will
 * expand to __start___llvm_prof_data
 */
#define INSTR_PROF_SECT_START(Sect) \
        INSTR_PROF_CONCAT(__start_,Sect)
#define INSTR_PROF_SECT_STOP(Sect) \
        INSTR_PROF_CONCAT(__stop_,Sect)

/* Value Profiling API linkage name.  */
#define INSTR_PROF_VALUE_PROF_FUNC __llvm_profile_instrument_target
#define INSTR_PROF_VALUE_PROF_FUNC_STR \
        INSTR_PROF_QUOTE(INSTR_PROF_VALUE_PROF_FUNC)
#define INSTR_PROF_VALUE_RANGE_PROF_FUNC __llvm_profile_instrument_range
#define INSTR_PROF_VALUE_RANGE_PROF_FUNC_STR \
        INSTR_PROF_QUOTE(INSTR_PROF_VALUE_RANGE_PROF_FUNC)

/* InstrProfile per-function control data alignment.  */
#define INSTR_PROF_DATA_ALIGNMENT 8

/* The data structure that represents a tracked value by the
 * value profiler.
 */
typedef struct InstrProfValueData {
  /* Profiled value. */
  uint64_t Value;
  /* Number of times the value appears in the training run. */
  uint64_t Count;
} InstrProfValueData;

#endif /* INSTR_PROF_DATA_INC */

#else
#undef INSTR_PROF_DATA_DEFINED
#endif
