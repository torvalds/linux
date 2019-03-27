//===-- SBValue.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBValue_h_
#define LLDB_SBValue_h_

#include "lldb/API/SBData.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBType.h"

class ValueImpl;
class ValueLocker;

namespace lldb {

class LLDB_API SBValue {
public:
  SBValue();

  SBValue(const lldb::SBValue &rhs);

  lldb::SBValue &operator=(const lldb::SBValue &rhs);

  ~SBValue();

  bool IsValid();

  void Clear();

  SBError GetError();

  lldb::user_id_t GetID();

  const char *GetName();

  const char *GetTypeName();

  const char *GetDisplayTypeName();

  size_t GetByteSize();

  bool IsInScope();

  lldb::Format GetFormat();

  void SetFormat(lldb::Format format);

  const char *GetValue();

  int64_t GetValueAsSigned(lldb::SBError &error, int64_t fail_value = 0);

  uint64_t GetValueAsUnsigned(lldb::SBError &error, uint64_t fail_value = 0);

  int64_t GetValueAsSigned(int64_t fail_value = 0);

  uint64_t GetValueAsUnsigned(uint64_t fail_value = 0);

  ValueType GetValueType();

  // If you call this on a newly created ValueObject, it will always return
  // false.
  bool GetValueDidChange();

  const char *GetSummary();

  const char *GetSummary(lldb::SBStream &stream,
                         lldb::SBTypeSummaryOptions &options);

  const char *GetObjectDescription();

  const char *GetTypeValidatorResult();

  lldb::SBValue GetDynamicValue(lldb::DynamicValueType use_dynamic);

  lldb::SBValue GetStaticValue();

  lldb::SBValue GetNonSyntheticValue();

  lldb::DynamicValueType GetPreferDynamicValue();

  void SetPreferDynamicValue(lldb::DynamicValueType use_dynamic);

  bool GetPreferSyntheticValue();

  void SetPreferSyntheticValue(bool use_synthetic);

  bool IsDynamic();

  bool IsSynthetic();

  bool IsSyntheticChildrenGenerated();

  void SetSyntheticChildrenGenerated(bool);

  const char *GetLocation();

  // Deprecated - use the one that takes SBError&
  bool SetValueFromCString(const char *value_str);

  bool SetValueFromCString(const char *value_str, lldb::SBError &error);

  lldb::SBTypeFormat GetTypeFormat();

  lldb::SBTypeSummary GetTypeSummary();

  lldb::SBTypeFilter GetTypeFilter();

#ifndef LLDB_DISABLE_PYTHON
  lldb::SBTypeSynthetic GetTypeSynthetic();
#endif

  lldb::SBValue GetChildAtIndex(uint32_t idx);

  lldb::SBValue CreateChildAtOffset(const char *name, uint32_t offset,
                                    lldb::SBType type);

  // Deprecated - use the expression evaluator to perform type casting
  lldb::SBValue Cast(lldb::SBType type);

  lldb::SBValue CreateValueFromExpression(const char *name,
                                          const char *expression);

  lldb::SBValue CreateValueFromExpression(const char *name,
                                          const char *expression,
                                          SBExpressionOptions &options);

  lldb::SBValue CreateValueFromAddress(const char *name, lldb::addr_t address,
                                       lldb::SBType type);

  // this has no address! GetAddress() and GetLoadAddress() as well as
  // AddressOf() on the return of this call all return invalid
  lldb::SBValue CreateValueFromData(const char *name, lldb::SBData data,
                                    lldb::SBType type);

  //------------------------------------------------------------------
  /// Get a child value by index from a value.
  ///
  /// Structs, unions, classes, arrays and pointers have child
  /// values that can be access by index.
  ///
  /// Structs and unions access child members using a zero based index
  /// for each child member. For
  ///
  /// Classes reserve the first indexes for base classes that have
  /// members (empty base classes are omitted), and all members of the
  /// current class will then follow the base classes.
  ///
  /// Pointers differ depending on what they point to. If the pointer
  /// points to a simple type, the child at index zero
  /// is the only child value available, unless \a synthetic_allowed
  /// is \b true, in which case the pointer will be used as an array
  /// and can create 'synthetic' child values using positive or
  /// negative indexes. If the pointer points to an aggregate type
  /// (an array, class, union, struct), then the pointee is
  /// transparently skipped and any children are going to be the indexes
  /// of the child values within the aggregate type. For example if
  /// we have a 'Point' type and we have a SBValue that contains a
  /// pointer to a 'Point' type, then the child at index zero will be
  /// the 'x' member, and the child at index 1 will be the 'y' member
  /// (the child at index zero won't be a 'Point' instance).
  ///
  /// If you actually need an SBValue that represents the type pointed
  /// to by a SBValue for which GetType().IsPointeeType() returns true,
  /// regardless of the pointee type, you can do that with SBValue::Dereference.
  ///
  /// Arrays have a preset number of children that can be accessed by
  /// index and will returns invalid child values for indexes that are
  /// out of bounds unless the \a synthetic_allowed is \b true. In this
  /// case the array can create 'synthetic' child values for indexes
  /// that aren't in the array bounds using positive or negative
  /// indexes.
  ///
  /// @param[in] idx
  ///     The index of the child value to get
  ///
  /// @param[in] use_dynamic
  ///     An enumeration that specifies whether to get dynamic values,
  ///     and also if the target can be run to figure out the dynamic
  ///     type of the child value.
  ///
  /// @param[in] can_create_synthetic
  ///     If \b true, then allow child values to be created by index
  ///     for pointers and arrays for indexes that normally wouldn't
  ///     be allowed.
  ///
  /// @return
  ///     A new SBValue object that represents the child member value.
  //------------------------------------------------------------------
  lldb::SBValue GetChildAtIndex(uint32_t idx,
                                lldb::DynamicValueType use_dynamic,
                                bool can_create_synthetic);

  // Matches children of this object only and will match base classes and
  // member names if this is a clang typed object.
  uint32_t GetIndexOfChildWithName(const char *name);

  // Matches child members of this object and child members of any base
  // classes.
  lldb::SBValue GetChildMemberWithName(const char *name);

  // Matches child members of this object and child members of any base
  // classes.
  lldb::SBValue GetChildMemberWithName(const char *name,
                                       lldb::DynamicValueType use_dynamic);

  // Expands nested expressions like .a->b[0].c[1]->d
  lldb::SBValue GetValueForExpressionPath(const char *expr_path);

  lldb::SBValue AddressOf();

  lldb::addr_t GetLoadAddress();

  lldb::SBAddress GetAddress();

  //------------------------------------------------------------------
  /// Get an SBData wrapping what this SBValue points to.
  ///
  /// This method will dereference the current SBValue, if its
  /// data type is a T* or T[], and extract item_count elements
  /// of type T from it, copying their contents in an SBData.
  ///
  /// @param[in] item_idx
  ///     The index of the first item to retrieve. For an array
  ///     this is equivalent to array[item_idx], for a pointer
  ///     to *(pointer + item_idx). In either case, the measurement
  ///     unit for item_idx is the sizeof(T) rather than the byte
  ///
  /// @param[in] item_count
  ///     How many items should be copied into the output. By default
  ///     only one item is copied, but more can be asked for.
  ///
  /// @return
  ///     An SBData with the contents of the copied items, on success.
  ///     An empty SBData otherwise.
  //------------------------------------------------------------------
  lldb::SBData GetPointeeData(uint32_t item_idx = 0, uint32_t item_count = 1);

  //------------------------------------------------------------------
  /// Get an SBData wrapping the contents of this SBValue.
  ///
  /// This method will read the contents of this object in memory
  /// and copy them into an SBData for future use.
  ///
  /// @return
  ///     An SBData with the contents of this SBValue, on success.
  ///     An empty SBData otherwise.
  //------------------------------------------------------------------
  lldb::SBData GetData();

  bool SetData(lldb::SBData &data, lldb::SBError &error);

  lldb::SBDeclaration GetDeclaration();

  //------------------------------------------------------------------
  /// Find out if a SBValue might have children.
  ///
  /// This call is much more efficient than GetNumChildren() as it
  /// doesn't need to complete the underlying type. This is designed
  /// to be used in a UI environment in order to detect if the
  /// disclosure triangle should be displayed or not.
  ///
  /// This function returns true for class, union, structure,
  /// pointers, references, arrays and more. Again, it does so without
  /// doing any expensive type completion.
  ///
  /// @return
  ///     Returns \b true if the SBValue might have children, or \b
  ///     false otherwise.
  //------------------------------------------------------------------
  bool MightHaveChildren();

  bool IsRuntimeSupportValue();

  uint32_t GetNumChildren();

  uint32_t GetNumChildren(uint32_t max);

  void *GetOpaqueType();

  lldb::SBTarget GetTarget();

  lldb::SBProcess GetProcess();

  lldb::SBThread GetThread();

  lldb::SBFrame GetFrame();

  lldb::SBValue Dereference();

  // Deprecated - please use GetType().IsPointerType() instead.
  bool TypeIsPointerType();

  lldb::SBType GetType();

  lldb::SBValue Persist();

  bool GetDescription(lldb::SBStream &description);

  bool GetExpressionPath(lldb::SBStream &description);

  bool GetExpressionPath(lldb::SBStream &description,
                         bool qualify_cxx_base_classes);

  SBValue(const lldb::ValueObjectSP &value_sp);

  //------------------------------------------------------------------
  /// Watch this value if it resides in memory.
  ///
  /// Sets a watchpoint on the value.
  ///
  /// @param[in] resolve_location
  ///     Resolve the location of this value once and watch its address.
  ///     This value must currently be set to \b true as watching all
  ///     locations of a variable or a variable path is not yet supported,
  ///     though we plan to support it in the future.
  ///
  /// @param[in] read
  ///     Stop when this value is accessed.
  ///
  /// @param[in] write
  ///     Stop when this value is modified
  ///
  /// @param[out] error
  ///     An error object. Contains the reason if there is some failure.
  ///
  /// @return
  ///     An SBWatchpoint object. This object might not be valid upon
  ///     return due to a value not being contained in memory, too
  ///     large, or watchpoint resources are not available or all in
  ///     use.
  //------------------------------------------------------------------
  lldb::SBWatchpoint Watch(bool resolve_location, bool read, bool write,
                           SBError &error);

  // Backward compatibility fix in the interim.
  lldb::SBWatchpoint Watch(bool resolve_location, bool read, bool write);

  //------------------------------------------------------------------
  /// Watch this value that this value points to in memory
  ///
  /// Sets a watchpoint on the value.
  ///
  /// @param[in] resolve_location
  ///     Resolve the location of this value once and watch its address.
  ///     This value must currently be set to \b true as watching all
  ///     locations of a variable or a variable path is not yet supported,
  ///     though we plan to support it in the future.
  ///
  /// @param[in] read
  ///     Stop when this value is accessed.
  ///
  /// @param[in] write
  ///     Stop when this value is modified
  ///
  /// @param[out] error
  ///     An error object. Contains the reason if there is some failure.
  ///
  /// @return
  ///     An SBWatchpoint object. This object might not be valid upon
  ///     return due to a value not being contained in memory, too
  ///     large, or watchpoint resources are not available or all in
  ///     use.
  //------------------------------------------------------------------
  lldb::SBWatchpoint WatchPointee(bool resolve_location, bool read, bool write,
                                  SBError &error);

  //------------------------------------------------------------------
  /// Same as the protected version of GetSP that takes a locker, except that we
  /// make the
  /// locker locally in the function.  Since the Target API mutex is recursive,
  /// and the
  /// StopLocker is a read lock, you can call this function even if you are
  /// already
  /// holding the two above-mentioned locks.
  ///
  /// @return
  ///     A ValueObjectSP of the best kind (static, dynamic or synthetic) we
  ///     can cons up, in accordance with the SBValue's settings.
  //------------------------------------------------------------------
  lldb::ValueObjectSP GetSP() const;

protected:
  friend class SBBlock;
  friend class SBFrame;
  friend class SBTarget;
  friend class SBThread;
  friend class SBValueList;

  //------------------------------------------------------------------
  /// Get the appropriate ValueObjectSP from this SBValue, consulting the
  /// use_dynamic and use_synthetic options passed in to SetSP when the
  /// SBValue's contents were set.  Since this often requires examining memory,
  /// and maybe even running code, it needs to acquire the Target API and
  /// Process StopLock.
  /// Those are held in an opaque class ValueLocker which is currently local to
  /// SBValue.cpp.
  /// So you don't have to get these yourself just default construct a
  /// ValueLocker, and pass it into this.
  /// If we need to make a ValueLocker and use it in some other .cpp file, we'll
  /// have to move it to
  /// ValueObject.h/cpp or somewhere else convenient.  We haven't needed to so
  /// far.
  ///
  /// @param[in] value_locker
  ///     An object that will hold the Target API, and Process RunLocks, and
  ///     auto-destroy them when it goes out of scope.  Currently this is only
  ///     useful in
  ///     SBValue.cpp.
  ///
  /// @return
  ///     A ValueObjectSP of the best kind (static, dynamic or synthetic) we
  ///     can cons up, in accordance with the SBValue's settings.
  //------------------------------------------------------------------
  lldb::ValueObjectSP GetSP(ValueLocker &value_locker) const;

  // these calls do the right thing WRT adjusting their settings according to
  // the target's preferences
  void SetSP(const lldb::ValueObjectSP &sp);

  void SetSP(const lldb::ValueObjectSP &sp, bool use_synthetic);

  void SetSP(const lldb::ValueObjectSP &sp, lldb::DynamicValueType use_dynamic);

  void SetSP(const lldb::ValueObjectSP &sp, lldb::DynamicValueType use_dynamic,
             bool use_synthetic);

  void SetSP(const lldb::ValueObjectSP &sp, lldb::DynamicValueType use_dynamic,
             bool use_synthetic, const char *name);

private:
  typedef std::shared_ptr<ValueImpl> ValueImplSP;
  ValueImplSP m_opaque_sp;

  void SetSP(ValueImplSP impl_sp);
};

} // namespace lldb

#endif // LLDB_SBValue_h_
