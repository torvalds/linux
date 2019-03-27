//===-- AddressRange.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AddressRange_h_
#define liblldb_AddressRange_h_

#include "lldb/Core/Address.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <stddef.h>

namespace lldb_private {
class SectionList;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class Target;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class AddressRange AddressRange.h "lldb/Core/AddressRange.h"
/// A section + offset based address range class.
//----------------------------------------------------------------------
class AddressRange {
public:
  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Initialize with a invalid section (NULL), an invalid offset
  /// (LLDB_INVALID_ADDRESS), and zero byte size.
  //------------------------------------------------------------------
  AddressRange();

  //------------------------------------------------------------------
  /// Construct with a section pointer, offset, and byte_size.
  ///
  /// Initialize the address with the supplied \a section, \a offset and \a
  /// byte_size.
  ///
  /// @param[in] section
  ///     A section pointer to a valid lldb::Section, or NULL if the
  ///     address doesn't have a section or will get resolved later.
  ///
  /// @param[in] offset
  ///     The offset in bytes into \a section.
  ///
  /// @param[in] byte_size
  ///     The size in bytes of the address range.
  //------------------------------------------------------------------
  AddressRange(const lldb::SectionSP &section, lldb::addr_t offset,
               lldb::addr_t byte_size);

  //------------------------------------------------------------------
  /// Construct with a virtual address, section list and byte size.
  ///
  /// Initialize and resolve the address with the supplied virtual address \a
  /// file_addr, and byte size \a byte_size.
  ///
  /// @param[in] file_addr
  ///     A virtual address.
  ///
  /// @param[in] byte_size
  ///     The size in bytes of the address range.
  ///
  /// @param[in] section_list
  ///     A list of sections, one of which may contain the \a vaddr.
  //------------------------------------------------------------------
  AddressRange(lldb::addr_t file_addr, lldb::addr_t byte_size,
               const SectionList *section_list = nullptr);

  //------------------------------------------------------------------
  /// Construct with a Address object address and byte size.
  ///
  /// Initialize by copying the section offset address in \a so_addr, and
  /// setting the byte size to \a byte_size.
  ///
  /// @param[in] so_addr
  ///     A section offset address object.
  ///
  /// @param[in] byte_size
  ///     The size in bytes of the address range.
  //------------------------------------------------------------------
  AddressRange(const Address &so_addr, lldb::addr_t byte_size);

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// The destructor is virtual in case this class is subclassed.
  //------------------------------------------------------------------
  ~AddressRange();

  //------------------------------------------------------------------
  /// Clear the object's state.
  ///
  /// Sets the section to an invalid value (NULL), an invalid offset
  /// (LLDB_INVALID_ADDRESS) and a zero byte size.
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  /// Check if a section offset address is contained in this range.
  ///
  /// @param[in] so_addr
  ///     A section offset address object reference.
  ///
  /// @return
  ///     Returns \b true if \a so_addr is contained in this range,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  //    bool
  //    Contains (const Address &so_addr) const;

  //------------------------------------------------------------------
  /// Check if a section offset address is contained in this range.
  ///
  /// @param[in] so_addr_ptr
  ///     A section offset address object pointer.
  ///
  /// @return
  ///     Returns \b true if \a so_addr is contained in this range,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  //    bool
  //    Contains (const Address *so_addr_ptr) const;

  //------------------------------------------------------------------
  /// Check if a section offset \a so_addr when represented as a file address
  /// is contained within this object's file address range.
  ///
  /// @param[in] so_addr
  ///     A section offset address object reference.
  ///
  /// @return
  ///     Returns \b true if both \a this and \a so_addr have
  ///     resolvable file address values and \a so_addr is contained
  ///     in the address range, \b false otherwise.
  //------------------------------------------------------------------
  bool ContainsFileAddress(const Address &so_addr) const;

  //------------------------------------------------------------------
  /// Check if the resolved file address \a file_addr is contained within this
  /// object's file address range.
  ///
  /// @param[in] so_addr
  ///     A section offset address object reference.
  ///
  /// @return
  ///     Returns \b true if both \a this has a resolvable file
  ///     address value and \a so_addr is contained in the address
  ///     range, \b false otherwise.
  //------------------------------------------------------------------
  bool ContainsFileAddress(lldb::addr_t file_addr) const;

  //------------------------------------------------------------------
  /// Check if a section offset \a so_addr when represented as a load address
  /// is contained within this object's load address range.
  ///
  /// @param[in] so_addr
  ///     A section offset address object reference.
  ///
  /// @return
  ///     Returns \b true if both \a this and \a so_addr have
  ///     resolvable load address values and \a so_addr is contained
  ///     in the address range, \b false otherwise.
  //------------------------------------------------------------------
  bool ContainsLoadAddress(const Address &so_addr, Target *target) const;

  //------------------------------------------------------------------
  /// Check if the resolved load address \a load_addr is contained within this
  /// object's load address range.
  ///
  /// @param[in] so_addr
  ///     A section offset address object reference.
  ///
  /// @return
  ///     Returns \b true if both \a this has a resolvable load
  ///     address value and \a so_addr is contained in the address
  ///     range, \b false otherwise.
  //------------------------------------------------------------------
  bool ContainsLoadAddress(lldb::addr_t load_addr, Target *target) const;

  //------------------------------------------------------------------
  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s. There are many ways to display a section offset based address
  /// range, and \a style lets the user choose how the base address gets
  /// displayed.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// @param[in] style
  ///     The display style for the address.
  ///
  /// @return
  ///     Returns \b true if the address was able to be displayed.
  ///     File and load addresses may be unresolved and it may not be
  ///     possible to display a valid value, \b false will be returned
  ///     in such cases.
  ///
  /// @see Address::DumpStyle
  //------------------------------------------------------------------
  bool
  Dump(Stream *s, Target *target, Address::DumpStyle style,
       Address::DumpStyle fallback_style = Address::DumpStyleInvalid) const;

  //------------------------------------------------------------------
  /// Dump a debug description of this object to a Stream.
  ///
  /// Dump a debug description of the contents of this object to the supplied
  /// stream \a s.
  ///
  /// The debug description contains verbose internal state such and pointer
  /// values, reference counts, etc.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  //------------------------------------------------------------------
  void DumpDebug(Stream *s) const;

  //------------------------------------------------------------------
  /// Get accessor for the base address of the range.
  ///
  /// @return
  ///     A reference to the base address object.
  //------------------------------------------------------------------
  Address &GetBaseAddress() { return m_base_addr; }

  //------------------------------------------------------------------
  /// Get const accessor for the base address of the range.
  ///
  /// @return
  ///     A const reference to the base address object.
  //------------------------------------------------------------------
  const Address &GetBaseAddress() const { return m_base_addr; }

  //------------------------------------------------------------------
  /// Get accessor for the byte size of this range.
  ///
  /// @return
  ///     The size in bytes of this address range.
  //------------------------------------------------------------------
  lldb::addr_t GetByteSize() const { return m_byte_size; }

  //------------------------------------------------------------------
  /// Get the memory cost of this object.
  ///
  /// @return
  ///     The number of bytes that this object occupies in memory.
  //------------------------------------------------------------------
  size_t MemorySize() const {
    // Noting special for the memory size of a single AddressRange object, it
    // is just the size of itself.
    return sizeof(AddressRange);
  }

  //------------------------------------------------------------------
  /// Set accessor for the byte size of this range.
  ///
  /// @param[in] byte_size
  ///     The new size in bytes of this address range.
  //------------------------------------------------------------------
  void SetByteSize(lldb::addr_t byte_size) { m_byte_size = byte_size; }

protected:
  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  Address m_base_addr;      ///< The section offset base address of this range.
  lldb::addr_t m_byte_size; ///< The size in bytes of this address range.
};

// bool operator== (const AddressRange& lhs, const AddressRange& rhs);

} // namespace lldb_private

#endif // liblldb_AddressRange_h_
