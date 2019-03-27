//===-- Address.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Address_h_
#define liblldb_Address_h_

#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class Block;
}
namespace lldb_private {
class CompileUnit;
}
namespace lldb_private {
class ExecutionContextScope;
}
namespace lldb_private {
class Function;
}
namespace lldb_private {
class SectionList;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class Symbol;
}
namespace lldb_private {
class SymbolContext;
}
namespace lldb_private {
class Target;
}
namespace lldb_private {
struct LineEntry;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Address Address.h "lldb/Core/Address.h"
/// A section + offset based address class.
///
/// The Address class allows addresses to be relative to a section that can
/// move during runtime due to images (executables, shared libraries, bundles,
/// frameworks) being loaded at different addresses than the addresses found
/// in the object file that represents them on disk. There are currently two
/// types of addresses for a section:
///     @li file addresses
///     @li load addresses
///
/// File addresses represent the virtual addresses that are in the "on disk"
/// object files. These virtual addresses are converted to be relative to
/// unique sections scoped to the object file so that when/if the addresses
/// slide when the images are loaded/unloaded in memory, we can easily track
/// these changes without having to update every object (compile unit ranges,
/// line tables, function address ranges, lexical block and inlined subroutine
/// address ranges, global and static variables) each time an image is loaded
/// or unloaded.
///
/// Load addresses represent the virtual addresses where each section ends up
/// getting loaded at runtime. Before executing a program, it is common for
/// all of the load addresses to be unresolved. When a DynamicLoader plug-in
/// receives notification that shared libraries have been loaded/unloaded, the
/// load addresses of the main executable and any images (shared libraries)
/// will be  resolved/unresolved. When this happens, breakpoints that are in
/// one of these sections can be set/cleared.
//----------------------------------------------------------------------
class Address {
public:
  //------------------------------------------------------------------
  /// Dump styles allow the Address::Dump(Stream *,DumpStyle) const function
  /// to display Address contents in a variety of ways.
  //------------------------------------------------------------------
  typedef enum {
    DumpStyleInvalid,           ///< Invalid dump style
    DumpStyleSectionNameOffset, ///< Display as the section name + offset.
                                ///< \code
    /// // address for printf in libSystem.B.dylib as a section name + offset
    /// libSystem.B.dylib.__TEXT.__text + 0x0005cfdf \endcode
    DumpStyleSectionPointerOffset, ///< Display as the section pointer + offset
                                   ///(debug output).
                                   ///< \code
    /// // address for printf in libSystem.B.dylib as a section pointer +
    /// offset (lldb::Section *)0x35cc50 + 0x000000000005cfdf \endcode
    DumpStyleFileAddress, ///< Display as the file address (if any).
                          ///< \code
    /// // address for printf in libSystem.B.dylib as a file address
    /// 0x000000000005dcff \endcode
    DumpStyleModuleWithFileAddress, ///< Display as the file address with the
                                    ///module name prepended (if any).
                                    ///< \code
    /// // address for printf in libSystem.B.dylib as a file address
    /// libSystem.B.dylib[0x000000000005dcff] \endcode
    DumpStyleLoadAddress, ///< Display as the load address (if resolved).
                          ///< \code
    /// // address for printf in libSystem.B.dylib as a load address
    /// 0x00007fff8306bcff \endcode
    DumpStyleResolvedDescription, ///< Display the details about what an address
                                  ///resolves to. This can
    ///< be anything from a symbol context summary (module, function/symbol,
    ///< and file and line), to information about what the pointer points to
    ///< if the address is in a section (section of pointers, c strings, etc).
    DumpStyleResolvedDescriptionNoModule,
    DumpStyleResolvedDescriptionNoFunctionArguments,
    DumpStyleNoFunctionName, ///< Elide the function name; display an offset
                             ///into the current function.
                             ///< Used primarily in disassembly symbolication
    DumpStyleDetailedSymbolContext, ///< Detailed symbol context information for
                                    ///an address for all symbol
                                    ///< context members.
    DumpStyleResolvedPointerDescription ///< Dereference a pointer at the
                                        ///current address and then lookup the
    ///< dereferenced address using DumpStyleResolvedDescription
  } DumpStyle;

  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Initialize with a invalid section (NULL) and an invalid offset
  /// (LLDB_INVALID_ADDRESS).
  //------------------------------------------------------------------
  Address() : m_section_wp(), m_offset(LLDB_INVALID_ADDRESS) {}

  //------------------------------------------------------------------
  /// Copy constructor
  ///
  /// Makes a copy of the another Address object \a rhs.
  ///
  /// @param[in] rhs
  ///     A const Address object reference to copy.
  //------------------------------------------------------------------
  Address(const Address &rhs)
      : m_section_wp(rhs.m_section_wp), m_offset(rhs.m_offset) {}

  //------------------------------------------------------------------
  /// Construct with a section pointer and offset.
  ///
  /// Initialize the address with the supplied \a section and \a offset.
  ///
  /// @param[in] section
  ///     A section pointer to a valid lldb::Section, or NULL if the
  ///     address doesn't have a section or will get resolved later.
  ///
  /// @param[in] offset
  ///     The offset in bytes into \a section.
  //------------------------------------------------------------------
  Address(const lldb::SectionSP &section_sp, lldb::addr_t offset)
      : m_section_wp(), // Don't init with section_sp in case section_sp is
                        // invalid (the weak_ptr will throw)
        m_offset(offset) {
    if (section_sp)
      m_section_wp = section_sp;
  }

  //------------------------------------------------------------------
  /// Construct with a virtual address and section list.
  ///
  /// Initialize and resolve the address with the supplied virtual address \a
  /// file_addr.
  ///
  /// @param[in] file_addr
  ///     A virtual file address.
  ///
  /// @param[in] section_list
  ///     A list of sections, one of which may contain the \a file_addr.
  //------------------------------------------------------------------
  Address(lldb::addr_t file_addr, const SectionList *section_list);

  Address(lldb::addr_t abs_addr);

//------------------------------------------------------------------
/// Assignment operator.
///
/// Copies the address value from another Address object \a rhs into \a this
/// object.
///
/// @param[in] rhs
///     A const Address object reference to copy.
///
/// @return
///     A const Address object reference to \a this.
//------------------------------------------------------------------
#ifndef SWIG
  const Address &operator=(const Address &rhs);
#endif

  //------------------------------------------------------------------
  /// Clear the object's state.
  ///
  /// Sets the section to an invalid value (NULL) and an invalid offset
  /// (LLDB_INVALID_ADDRESS).
  //------------------------------------------------------------------
  void Clear() {
    m_section_wp.reset();
    m_offset = LLDB_INVALID_ADDRESS;
  }

  //------------------------------------------------------------------
  /// Compare two Address objects.
  ///
  /// @param[in] lhs
  ///     The Left Hand Side const Address object reference.
  ///
  /// @param[in] rhs
  ///     The Right Hand Side const Address object reference.
  ///
  /// @return
  ///     @li -1 if lhs < rhs
  ///     @li 0 if lhs == rhs
  ///     @li 1 if lhs > rhs
  //------------------------------------------------------------------
  static int CompareFileAddress(const Address &lhs, const Address &rhs);

  static int CompareLoadAddress(const Address &lhs, const Address &rhs,
                                Target *target);

  static int CompareModulePointerAndOffset(const Address &lhs,
                                           const Address &rhs);

  // For use with std::map, std::multi_map
  class ModulePointerAndOffsetLessThanFunctionObject {
  public:
    ModulePointerAndOffsetLessThanFunctionObject() = default;

    bool operator()(const Address &a, const Address &b) const {
      return Address::CompareModulePointerAndOffset(a, b) < 0;
    }
  };

  //------------------------------------------------------------------
  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s. There are many ways to display a section offset based address, and
  /// \a style lets the user choose.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// @param[in] style
  ///     The display style for the address.
  ///
  /// @param[in] fallback_style
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
  bool Dump(Stream *s, ExecutionContextScope *exe_scope, DumpStyle style,
            DumpStyle fallback_style = DumpStyleInvalid,
            uint32_t addr_byte_size = UINT32_MAX) const;

  AddressClass GetAddressClass() const;

  //------------------------------------------------------------------
  /// Get the file address.
  ///
  /// If an address comes from a file on disk that has section relative
  /// addresses, then it has a virtual address that is relative to unique
  /// section in the object file.
  ///
  /// @return
  ///     The valid file virtual address, or LLDB_INVALID_ADDRESS if
  ///     the address doesn't have a file virtual address (image is
  ///     from memory only with no representation on disk).
  //------------------------------------------------------------------
  lldb::addr_t GetFileAddress() const;

  //------------------------------------------------------------------
  /// Get the load address.
  ///
  /// If an address comes from a file on disk that has section relative
  /// addresses, then it has a virtual address that is relative to unique
  /// section in the object file. Sections get resolved at runtime by
  /// DynamicLoader plug-ins as images (executables and shared libraries) get
  /// loaded/unloaded. If a section is loaded, then the load address can be
  /// resolved.
  ///
  /// @return
  ///     The valid load virtual address, or LLDB_INVALID_ADDRESS if
  ///     the address is currently not loaded.
  //------------------------------------------------------------------
  lldb::addr_t GetLoadAddress(Target *target) const;

  //------------------------------------------------------------------
  /// Get the load address as a callable code load address.
  ///
  /// This function will first resolve its address to a load address. Then, if
  /// the address turns out to be in code address, return the load address
  /// that would be required to call or return to. The address might have
  /// extra bits set (bit zero will be set to Thumb functions for an ARM
  /// target) that are required when changing the program counter to setting a
  /// return address.
  ///
  /// @return
  ///     The valid load virtual address, or LLDB_INVALID_ADDRESS if
  ///     the address is currently not loaded.
  //------------------------------------------------------------------
  lldb::addr_t GetCallableLoadAddress(Target *target,
                                      bool is_indirect = false) const;

  //------------------------------------------------------------------
  /// Get the load address as an opcode load address.
  ///
  /// This function will first resolve its address to a load address. Then, if
  /// the address turns out to be in code address, return the load address for
  /// an opcode. This address object might have extra bits set (bit zero will
  /// be set to Thumb functions for an
  /// ARM target) that are required for changing the program counter
  /// and this function will remove any bits that are intended for these
  /// special purposes. The result of this function can be used to safely
  /// write a software breakpoint trap to memory.
  ///
  /// @return
  ///     The valid load virtual address with extra callable bits
  ///     removed, or LLDB_INVALID_ADDRESS if the address is currently
  ///     not loaded.
  //------------------------------------------------------------------
  lldb::addr_t GetOpcodeLoadAddress(
      Target *target,
      AddressClass addr_class = AddressClass::eInvalid) const;

  //------------------------------------------------------------------
  /// Get the section relative offset value.
  ///
  /// @return
  ///     The current offset, or LLDB_INVALID_ADDRESS if this address
  ///     doesn't contain a valid offset.
  //------------------------------------------------------------------
  lldb::addr_t GetOffset() const { return m_offset; }

  //------------------------------------------------------------------
  /// Check if an address is section offset.
  ///
  /// When converting a virtual file or load address into a section offset
  /// based address, we often need to know if, given a section list, if the
  /// address was able to be converted to section offset. This function
  /// returns true if the current value contained in this object is section
  /// offset based.
  ///
  /// @return
  ///     Returns \b true if the address has a valid section and
  ///     offset, \b false otherwise.
  //------------------------------------------------------------------
  bool IsSectionOffset() const {
    return IsValid() && (GetSection().get() != nullptr);
  }

  //------------------------------------------------------------------
  /// Check if the object state is valid.
  ///
  /// A valid Address object contains either a section pointer and
  /// offset (for section offset based addresses), or just a valid offset
  /// (for absolute addresses that have no section).
  ///
  /// @return
  ///     Returns \b true if the offset is valid, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  bool IsValid() const { return m_offset != LLDB_INVALID_ADDRESS; }

  //------------------------------------------------------------------
  /// Get the memory cost of this object.
  ///
  /// @return
  ///     The number of bytes that this object occupies in memory.
  //------------------------------------------------------------------
  size_t MemorySize() const;

  //------------------------------------------------------------------
  /// Resolve a file virtual address using a section list.
  ///
  /// Given a list of sections, attempt to resolve \a addr as an offset into
  /// one of the file sections.
  ///
  /// @return
  ///     Returns \b true if \a addr was able to be resolved, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  bool ResolveAddressUsingFileSections(lldb::addr_t addr,
                                       const SectionList *sections);

  //------------------------------------------------------------------
  /// Set the address to represent \a load_addr.
  ///
  /// The address will attempt to find a loaded section within \a target that
  /// contains \a load_addr. If successful, this address object will have a
  /// valid section and offset. Else this address object will have no section
  /// (NULL) and the offset will be \a load_addr.
  ///
  /// @param[in] load_addr
  ///     A load address from a current process.
  ///
  /// @param[in] target
  ///     The target to use when trying resolve the address into
  ///     a section + offset. The Target's SectionLoadList object
  ///     is used to resolve the address.
  ///
  /// @param[in] allow_section_end
  ///     If true, treat an address pointing to the end of the module as
  ///     belonging to that module.
  ///
  /// @return
  ///     Returns \b true if the load address was resolved to be
  ///     section/offset, \b false otherwise. It is often ok for an
  ///     address no not resolve to a section in a module, this often
  ///     happens for JIT'ed code, or any load addresses on the stack
  ///     or heap.
  //------------------------------------------------------------------
  bool SetLoadAddress(lldb::addr_t load_addr, Target *target,
                      bool allow_section_end = false);

  bool SetOpcodeLoadAddress(
      lldb::addr_t load_addr, Target *target,
      AddressClass addr_class = AddressClass::eInvalid,
      bool allow_section_end = false);

  bool SetCallableLoadAddress(lldb::addr_t load_addr, Target *target);

  //------------------------------------------------------------------
  /// Get accessor for the module for this address.
  ///
  /// @return
  ///     Returns the Module pointer that this address is an offset
  ///     in, or NULL if this address doesn't belong in a module, or
  ///     isn't resolved yet.
  //------------------------------------------------------------------
  lldb::ModuleSP GetModule() const;

  //------------------------------------------------------------------
  /// Get const accessor for the section.
  ///
  /// @return
  ///     Returns the const lldb::Section pointer that this address is an
  ///     offset in, or NULL if this address is absolute.
  //------------------------------------------------------------------
  lldb::SectionSP GetSection() const { return m_section_wp.lock(); }

  //------------------------------------------------------------------
  /// Set accessor for the offset.
  ///
  /// @param[in] offset
  ///     A new offset value for this object.
  ///
  /// @return
  ///     Returns \b true if the offset changed, \b false otherwise.
  //------------------------------------------------------------------
  bool SetOffset(lldb::addr_t offset) {
    bool changed = m_offset != offset;
    m_offset = offset;
    return changed;
  }

  void SetRawAddress(lldb::addr_t addr) {
    m_section_wp.reset();
    m_offset = addr;
  }

  bool Slide(int64_t offset) {
    if (m_offset != LLDB_INVALID_ADDRESS) {
      m_offset += offset;
      return true;
    }
    return false;
  }

  //------------------------------------------------------------------
  /// Set accessor for the section.
  ///
  /// @param[in] section
  ///     A new lldb::Section pointer to use as the section base. Can
  ///     be NULL for absolute addresses that are not relative to
  ///     any section.
  //------------------------------------------------------------------
  void SetSection(const lldb::SectionSP &section_sp) {
    m_section_wp = section_sp;
  }

  void ClearSection() { m_section_wp.reset(); }

  //------------------------------------------------------------------
  /// Reconstruct a symbol context from an address.
  ///
  /// This class doesn't inherit from SymbolContextScope because many address
  /// objects have short lifespans. Address objects that are section offset
  /// can reconstruct their symbol context by looking up the address in the
  /// module found in the section.
  ///
  /// @see SymbolContextScope::CalculateSymbolContext(SymbolContext*)
  //------------------------------------------------------------------
  uint32_t CalculateSymbolContext(SymbolContext *sc,
                                  lldb::SymbolContextItem resolve_scope =
                                      lldb::eSymbolContextEverything) const;

  lldb::ModuleSP CalculateSymbolContextModule() const;

  CompileUnit *CalculateSymbolContextCompileUnit() const;

  Function *CalculateSymbolContextFunction() const;

  Block *CalculateSymbolContextBlock() const;

  Symbol *CalculateSymbolContextSymbol() const;

  bool CalculateSymbolContextLineEntry(LineEntry &line_entry) const;

  //------------------------------------------------------------------
  // Returns true if the section should be valid, but isn't because the shared
  // pointer to the section can't be reconstructed from a weak pointer that
  // contains a valid weak reference to a section. Returns false if the section
  // weak pointer has no reference to a section, or if the section is still
  // valid
  //------------------------------------------------------------------
  bool SectionWasDeleted() const;

protected:
  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  lldb::SectionWP m_section_wp; ///< The section for the address, can be NULL.
  lldb::addr_t m_offset; ///< Offset into section if \a m_section_wp is valid...

  //------------------------------------------------------------------
  // Returns true if the m_section_wp once had a reference to a valid section
  // shared pointer, but no longer does. This can happen if we have an address
  // from a module that gets unloaded and deleted. This function should only be
  // called if GetSection() returns an empty shared pointer and you want to
  // know if this address used to have a valid section.
  //------------------------------------------------------------------
  bool SectionWasDeletedPrivate() const;
};

//----------------------------------------------------------------------
// NOTE: Be careful using this operator. It can correctly compare two
// addresses from the same Module correctly. It can't compare two addresses
// from different modules in any meaningful way, but it will compare the module
// pointers.
//
// To sum things up:
// - works great for addresses within the same module - it works for addresses
// across multiple modules, but don't expect the
//   address results to make much sense
//
// This basically lets Address objects be used in ordered collection classes.
//----------------------------------------------------------------------
bool operator<(const Address &lhs, const Address &rhs);
bool operator>(const Address &lhs, const Address &rhs);
bool operator==(const Address &lhs, const Address &rhs);
bool operator!=(const Address &lhs, const Address &rhs);

} // namespace lldb_private

#endif // liblldb_Address_h_
