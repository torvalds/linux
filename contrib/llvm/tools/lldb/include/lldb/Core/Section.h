//===-- Section.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Section_h_
#define liblldb_Section_h_

#include "lldb/Core/ModuleChild.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Flags.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <memory>
#include <vector>

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class Address;
}
namespace lldb_private {
class DataExtractor;
}
namespace lldb_private {
class ObjectFile;
}
namespace lldb_private {
class Section;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class Target;
}

namespace lldb_private {

class SectionList {
public:
  typedef std::vector<lldb::SectionSP> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  SectionList();

  ~SectionList();

  SectionList &operator=(const SectionList &rhs);

  size_t AddSection(const lldb::SectionSP &section_sp);

  size_t AddUniqueSection(const lldb::SectionSP &section_sp);

  size_t FindSectionIndex(const Section *sect);

  bool ContainsSection(lldb::user_id_t sect_id) const;

  void Dump(Stream *s, Target *target, bool show_header, uint32_t depth) const;

  lldb::SectionSP FindSectionByName(const ConstString &section_dstr) const;

  lldb::SectionSP FindSectionByID(lldb::user_id_t sect_id) const;

  lldb::SectionSP FindSectionByType(lldb::SectionType sect_type,
                                    bool check_children,
                                    size_t start_idx = 0) const;

  lldb::SectionSP
  FindSectionContainingFileAddress(lldb::addr_t addr,
                                   uint32_t depth = UINT32_MAX) const;

  // Get the number of sections in this list only
  size_t GetSize() const { return m_sections.size(); }

  // Get the number of sections in this list, and any contained child sections
  size_t GetNumSections(uint32_t depth) const;

  bool ReplaceSection(lldb::user_id_t sect_id,
                      const lldb::SectionSP &section_sp,
                      uint32_t depth = UINT32_MAX);

  // Warning, this can be slow as it's removing items from a std::vector.
  bool DeleteSection(size_t idx);

  lldb::SectionSP GetSectionAtIndex(size_t idx) const;

  size_t Slide(lldb::addr_t slide_amount, bool slide_children);

  void Clear() { m_sections.clear(); }

protected:
  collection m_sections;
};

class Section : public std::enable_shared_from_this<Section>,
                public ModuleChild,
                public UserID,
                public Flags {
public:
  // Create a root section (one that has no parent)
  Section(const lldb::ModuleSP &module_sp, ObjectFile *obj_file,
          lldb::user_id_t sect_id, const ConstString &name,
          lldb::SectionType sect_type, lldb::addr_t file_vm_addr,
          lldb::addr_t vm_size, lldb::offset_t file_offset,
          lldb::offset_t file_size, uint32_t log2align, uint32_t flags,
          uint32_t target_byte_size = 1);

  // Create a section that is a child of parent_section_sp
  Section(const lldb::SectionSP &parent_section_sp, // NULL for top level
                                                    // sections, non-NULL for
                                                    // child sections
          const lldb::ModuleSP &module_sp, ObjectFile *obj_file,
          lldb::user_id_t sect_id, const ConstString &name,
          lldb::SectionType sect_type, lldb::addr_t file_vm_addr,
          lldb::addr_t vm_size, lldb::offset_t file_offset,
          lldb::offset_t file_size, uint32_t log2align, uint32_t flags,
          uint32_t target_byte_size = 1);

  ~Section();

  static int Compare(const Section &a, const Section &b);

  bool ContainsFileAddress(lldb::addr_t vm_addr) const;

  SectionList &GetChildren() { return m_children; }

  const SectionList &GetChildren() const { return m_children; }

  void Dump(Stream *s, Target *target, uint32_t depth) const;

  void DumpName(Stream *s) const;

  lldb::addr_t GetLoadBaseAddress(Target *target) const;

  bool ResolveContainedAddress(lldb::addr_t offset, Address &so_addr,
                               bool allow_section_end = false) const;

  lldb::offset_t GetFileOffset() const { return m_file_offset; }

  void SetFileOffset(lldb::offset_t file_offset) {
    m_file_offset = file_offset;
  }

  lldb::offset_t GetFileSize() const { return m_file_size; }

  void SetFileSize(lldb::offset_t file_size) { m_file_size = file_size; }

  lldb::addr_t GetFileAddress() const;

  bool SetFileAddress(lldb::addr_t file_addr);

  lldb::addr_t GetOffset() const;

  lldb::addr_t GetByteSize() const { return m_byte_size; }

  void SetByteSize(lldb::addr_t byte_size) { m_byte_size = byte_size; }

  bool IsFake() const { return m_fake; }

  void SetIsFake(bool fake) { m_fake = fake; }

  bool IsEncrypted() const { return m_encrypted; }

  void SetIsEncrypted(bool b) { m_encrypted = b; }

  bool IsDescendant(const Section *section);

  const ConstString &GetName() const { return m_name; }

  bool Slide(lldb::addr_t slide_amount, bool slide_children);

  lldb::SectionType GetType() const { return m_type; }

  const char *GetTypeAsCString() const;

  lldb::SectionSP GetParent() const { return m_parent_wp.lock(); }

  bool IsThreadSpecific() const { return m_thread_specific; }

  void SetIsThreadSpecific(bool b) { m_thread_specific = b; }

  //------------------------------------------------------------------
  /// Get the permissions as OR'ed bits from lldb::Permissions
  //------------------------------------------------------------------
  uint32_t GetPermissions() const;

  //------------------------------------------------------------------
  /// Set the permissions using bits OR'ed from lldb::Permissions
  //------------------------------------------------------------------
  void SetPermissions(uint32_t permissions);

  ObjectFile *GetObjectFile() { return m_obj_file; }
  const ObjectFile *GetObjectFile() const { return m_obj_file; }

  //------------------------------------------------------------------
  /// Read the section data from the object file that the section
  /// resides in.
  ///
  /// @param[in] dst
  ///     Where to place the data
  ///
  /// @param[in] dst_len
  ///     How many bytes of section data to read
  ///
  /// @param[in] offset
  ///     The offset in bytes within this section's data at which to
  ///     start copying data from.
  ///
  /// @return
  ///     The number of bytes read from the section, or zero if the
  ///     section has no data or \a offset is not a valid offset
  ///     in this section.
  //------------------------------------------------------------------
  lldb::offset_t GetSectionData(void *dst, lldb::offset_t dst_len,
                                lldb::offset_t offset = 0);

  //------------------------------------------------------------------
  /// Get the shared reference to the section data from the object
  /// file that the section resides in. No copies of the data will be
  /// make unless the object file has been read from memory. If the
  /// object file is on disk, it will shared the mmap data for the
  /// entire object file.
  ///
  /// @param[in] data
  ///     Where to place the data, address byte size, and byte order
  ///
  /// @return
  ///     The number of bytes read from the section, or zero if the
  ///     section has no data or \a offset is not a valid offset
  ///     in this section.
  //------------------------------------------------------------------
  lldb::offset_t GetSectionData(DataExtractor &data);

  uint32_t GetLog2Align() { return m_log2align; }

  void SetLog2Align(uint32_t align) { m_log2align = align; }

  // Get the number of host bytes required to hold a target byte
  uint32_t GetTargetByteSize() const { return m_target_byte_size; }

  bool IsRelocated() const { return m_relocated; }

  void SetIsRelocated(bool b) { m_relocated = b; }

protected:
  ObjectFile *m_obj_file;   // The object file that data for this section should
                            // be read from
  lldb::SectionType m_type; // The type of this section
  lldb::SectionWP m_parent_wp; // Weak pointer to parent section
  ConstString m_name;          // Name of this section
  lldb::addr_t m_file_addr; // The absolute file virtual address range of this
                            // section if m_parent == NULL,
  // offset from parent file virtual address if m_parent != NULL
  lldb::addr_t m_byte_size; // Size in bytes that this section will occupy in
                            // memory at runtime
  lldb::offset_t m_file_offset; // Object file offset (if any)
  lldb::offset_t m_file_size;   // Object file size (can be smaller than
                                // m_byte_size for zero filled sections...)
  uint32_t m_log2align;   // log_2(align) of the section (i.e. section has to be
                          // aligned to 2^m_log2align)
  SectionList m_children; // Child sections
  bool m_fake : 1, // If true, then this section only can contain the address if
                   // one of its
      // children contains an address. This allows for gaps between the
      // children that are contained in the address range for this section, but
      // do not produce hits unless the children contain the address.
      m_encrypted : 1,         // Set to true if the contents are encrypted
      m_thread_specific : 1,   // This section is thread specific
      m_readable : 1,          // If this section has read permissions
      m_writable : 1,          // If this section has write permissions
      m_executable : 1,        // If this section has executable permissions
      m_relocated : 1;         // If this section has had relocations applied
  uint32_t m_target_byte_size; // Some architectures have non-8-bit byte size.
                               // This is specified as
                               // as a multiple number of a host bytes
private:
  DISALLOW_COPY_AND_ASSIGN(Section);
};

} // namespace lldb_private

#endif // liblldb_Section_h_
