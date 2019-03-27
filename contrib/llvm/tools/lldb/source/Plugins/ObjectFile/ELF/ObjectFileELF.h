//===-- ObjectFileELF.h --------------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ObjectFileELF_h_
#define liblldb_ObjectFileELF_h_

#include <stdint.h>

#include <vector>

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-private.h"

#include "ELFHeader.h"

struct ELFNote {
  elf::elf_word n_namesz;
  elf::elf_word n_descsz;
  elf::elf_word n_type;

  std::string n_name;

  ELFNote() : n_namesz(0), n_descsz(0), n_type(0) {}

  /// Parse an ELFNote entry from the given DataExtractor starting at position
  /// \p offset.
  ///
  /// @param[in] data
  ///    The DataExtractor to read from.
  ///
  /// @param[in,out] offset
  ///    Pointer to an offset in the data.  On return the offset will be
  ///    advanced by the number of bytes read.
  ///
  /// @return
  ///    True if the ELFRel entry was successfully read and false otherwise.
  bool Parse(const lldb_private::DataExtractor &data, lldb::offset_t *offset);

  size_t GetByteSize() const {
    return 12 + llvm::alignTo(n_namesz, 4) + llvm::alignTo(n_descsz, 4);
  }
};

//------------------------------------------------------------------------------
/// @class ObjectFileELF
/// Generic ELF object file reader.
///
/// This class provides a generic ELF (32/64 bit) reader plugin implementing
/// the ObjectFile protocol.
class ObjectFileELF : public lldb_private::ObjectFile {
public:
  ~ObjectFileELF() override;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  static lldb_private::ObjectFile *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                 lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                 lldb::offset_t file_offset, lldb::offset_t length);

  static lldb_private::ObjectFile *CreateMemoryInstance(
      const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
      const lldb::ProcessSP &process_sp, lldb::addr_t header_addr);

  static size_t GetModuleSpecifications(const lldb_private::FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        lldb_private::ModuleSpecList &specs);

  static bool MagicBytesMatch(lldb::DataBufferSP &data_sp, lldb::addr_t offset,
                              lldb::addr_t length);

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  //------------------------------------------------------------------
  // ObjectFile Protocol.
  //------------------------------------------------------------------
  bool ParseHeader() override;

  bool SetLoadAddress(lldb_private::Target &target, lldb::addr_t value,
                      bool value_is_offset) override;

  lldb::ByteOrder GetByteOrder() const override;

  bool IsExecutable() const override;

  uint32_t GetAddressByteSize() const override;

  lldb_private::AddressClass GetAddressClass(lldb::addr_t file_addr) override;

  lldb_private::Symtab *GetSymtab() override;

  bool IsStripped() override;

  void CreateSections(lldb_private::SectionList &unified_section_list) override;

  void Dump(lldb_private::Stream *s) override;

  lldb_private::ArchSpec GetArchitecture() override;

  bool GetUUID(lldb_private::UUID *uuid) override;

  lldb_private::FileSpecList GetDebugSymbolFilePaths() override;

  uint32_t GetDependentModules(lldb_private::FileSpecList &files) override;

  lldb_private::Address
  GetImageInfoAddress(lldb_private::Target *target) override;

  lldb_private::Address GetEntryPointAddress() override;

  lldb_private::Address GetBaseAddress() override;

  ObjectFile::Type CalculateType() override;

  ObjectFile::Strata CalculateStrata() override;

  size_t ReadSectionData(lldb_private::Section *section,
                         lldb::offset_t section_offset, void *dst,
                         size_t dst_len) override;

  size_t ReadSectionData(lldb_private::Section *section,
                         lldb_private::DataExtractor &section_data) override;

  llvm::ArrayRef<elf::ELFProgramHeader> ProgramHeaders();
  lldb_private::DataExtractor GetSegmentData(const elf::ELFProgramHeader &H);

  llvm::StringRef
  StripLinkerSymbolAnnotations(llvm::StringRef symbol_name) const override;

  void RelocateSection(lldb_private::Section *section) override;

protected:

  std::vector<LoadableData>
  GetLoadableData(lldb_private::Target &target) override;

private:
  ObjectFileELF(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                lldb::offset_t offset, lldb::offset_t length);

  ObjectFileELF(const lldb::ModuleSP &module_sp,
                lldb::DataBufferSP &header_data_sp,
                const lldb::ProcessSP &process_sp, lldb::addr_t header_addr);

  typedef std::vector<elf::ELFProgramHeader> ProgramHeaderColl;

  struct ELFSectionHeaderInfo : public elf::ELFSectionHeader {
    lldb_private::ConstString section_name;
  };

  typedef std::vector<ELFSectionHeaderInfo> SectionHeaderColl;
  typedef SectionHeaderColl::iterator SectionHeaderCollIter;
  typedef SectionHeaderColl::const_iterator SectionHeaderCollConstIter;

  typedef std::vector<elf::ELFDynamic> DynamicSymbolColl;
  typedef DynamicSymbolColl::iterator DynamicSymbolCollIter;
  typedef DynamicSymbolColl::const_iterator DynamicSymbolCollConstIter;

  typedef std::map<lldb::addr_t, lldb_private::AddressClass>
      FileAddressToAddressClassMap;

  /// Version of this reader common to all plugins based on this class.
  static const uint32_t m_plugin_version = 1;
  static const uint32_t g_core_uuid_magic;

  /// ELF file header.
  elf::ELFHeader m_header;

  /// ELF build ID.
  lldb_private::UUID m_uuid;

  /// ELF .gnu_debuglink file and crc data if available.
  std::string m_gnu_debuglink_file;
  uint32_t m_gnu_debuglink_crc;

  /// Collection of program headers.
  ProgramHeaderColl m_program_headers;

  /// Collection of section headers.
  SectionHeaderColl m_section_headers;

  /// Collection of symbols from the dynamic table.
  DynamicSymbolColl m_dynamic_symbols;

  /// List of file specifications corresponding to the modules (shared
  /// libraries) on which this object file depends.
  mutable std::unique_ptr<lldb_private::FileSpecList> m_filespec_ap;

  /// Cached value of the entry point for this module.
  lldb_private::Address m_entry_point_address;

  /// The architecture detected from parsing elf file contents.
  lldb_private::ArchSpec m_arch_spec;

  /// The address class for each symbol in the elf file
  FileAddressToAddressClassMap m_address_class_map;

  /// Returns the index of the given section header.
  size_t SectionIndex(const SectionHeaderCollIter &I);

  /// Returns the index of the given section header.
  size_t SectionIndex(const SectionHeaderCollConstIter &I) const;

  // Parses the ELF program headers.
  static size_t GetProgramHeaderInfo(ProgramHeaderColl &program_headers,
                                     lldb_private::DataExtractor &object_data,
                                     const elf::ELFHeader &header);

  // Finds PT_NOTE segments and calculates their crc sum.
  static uint32_t
  CalculateELFNotesSegmentsCRC32(const ProgramHeaderColl &program_headers,
                                 lldb_private::DataExtractor &data);

  /// Parses all section headers present in this object file and populates
  /// m_program_headers.  This method will compute the header list only once.
  /// Returns true iff the headers have been successfully parsed.
  bool ParseProgramHeaders();

  /// Parses all section headers present in this object file and populates
  /// m_section_headers.  This method will compute the header list only once.
  /// Returns the number of headers parsed.
  size_t ParseSectionHeaders();

  lldb::SectionType GetSectionType(const ELFSectionHeaderInfo &H) const;

  static void ParseARMAttributes(lldb_private::DataExtractor &data,
                                 uint64_t length,
                                 lldb_private::ArchSpec &arch_spec);

  /// Parses the elf section headers and returns the uuid, debug link name,
  /// crc, archspec.
  static size_t GetSectionHeaderInfo(SectionHeaderColl &section_headers,
                                     lldb_private::DataExtractor &object_data,
                                     const elf::ELFHeader &header,
                                     lldb_private::UUID &uuid,
                                     std::string &gnu_debuglink_file,
                                     uint32_t &gnu_debuglink_crc,
                                     lldb_private::ArchSpec &arch_spec);

  /// Scans the dynamic section and locates all dependent modules (shared
  /// libraries) populating m_filespec_ap.  This method will compute the
  /// dependent module list only once.  Returns the number of dependent
  /// modules parsed.
  size_t ParseDependentModules();

  /// Parses the dynamic symbol table and populates m_dynamic_symbols.  The
  /// vector retains the order as found in the object file.  Returns the
  /// number of dynamic symbols parsed.
  size_t ParseDynamicSymbols();

  /// Populates m_symtab_ap will all non-dynamic linker symbols.  This method
  /// will parse the symbols only once.  Returns the number of symbols parsed.
  unsigned ParseSymbolTable(lldb_private::Symtab *symbol_table,
                            lldb::user_id_t start_id,
                            lldb_private::Section *symtab);

  /// Helper routine for ParseSymbolTable().
  unsigned ParseSymbols(lldb_private::Symtab *symbol_table,
                        lldb::user_id_t start_id,
                        lldb_private::SectionList *section_list,
                        const size_t num_symbols,
                        const lldb_private::DataExtractor &symtab_data,
                        const lldb_private::DataExtractor &strtab_data);

  /// Scans the relocation entries and adds a set of artificial symbols to the
  /// given symbol table for each PLT slot.  Returns the number of symbols
  /// added.
  unsigned ParseTrampolineSymbols(lldb_private::Symtab *symbol_table,
                                  lldb::user_id_t start_id,
                                  const ELFSectionHeaderInfo *rela_hdr,
                                  lldb::user_id_t section_id);

  void ParseUnwindSymbols(lldb_private::Symtab *symbol_table,
                          lldb_private::DWARFCallFrameInfo *eh_frame);

  /// Relocates debug sections
  unsigned RelocateDebugSections(const elf::ELFSectionHeader *rel_hdr,
                                 lldb::user_id_t rel_id,
                                 lldb_private::Symtab *thetab);

  unsigned ApplyRelocations(lldb_private::Symtab *symtab,
                            const elf::ELFHeader *hdr,
                            const elf::ELFSectionHeader *rel_hdr,
                            const elf::ELFSectionHeader *symtab_hdr,
                            const elf::ELFSectionHeader *debug_hdr,
                            lldb_private::DataExtractor &rel_data,
                            lldb_private::DataExtractor &symtab_data,
                            lldb_private::DataExtractor &debug_data,
                            lldb_private::Section *rel_section);

  /// Loads the section name string table into m_shstr_data.  Returns the
  /// number of bytes constituting the table.
  size_t GetSectionHeaderStringTable();

  /// Utility method for looking up a section given its name.  Returns the
  /// index of the corresponding section or zero if no section with the given
  /// name can be found (note that section indices are always 1 based, and so
  /// section index 0 is never valid).
  lldb::user_id_t GetSectionIndexByName(const char *name);

  // Returns the ID of the first section that has the given type.
  lldb::user_id_t GetSectionIndexByType(unsigned type);

  /// Returns the section header with the given id or NULL.
  const ELFSectionHeaderInfo *GetSectionHeaderByIndex(lldb::user_id_t id);

  /// @name  ELF header dump routines
  //@{
  static void DumpELFHeader(lldb_private::Stream *s,
                            const elf::ELFHeader &header);

  static void DumpELFHeader_e_ident_EI_DATA(lldb_private::Stream *s,
                                            unsigned char ei_data);

  static void DumpELFHeader_e_type(lldb_private::Stream *s,
                                   elf::elf_half e_type);
  //@}

  /// @name ELF program header dump routines
  //@{
  void DumpELFProgramHeaders(lldb_private::Stream *s);

  static void DumpELFProgramHeader(lldb_private::Stream *s,
                                   const elf::ELFProgramHeader &ph);

  static void DumpELFProgramHeader_p_type(lldb_private::Stream *s,
                                          elf::elf_word p_type);

  static void DumpELFProgramHeader_p_flags(lldb_private::Stream *s,
                                           elf::elf_word p_flags);
  //@}

  /// @name ELF section header dump routines
  //@{
  void DumpELFSectionHeaders(lldb_private::Stream *s);

  static void DumpELFSectionHeader(lldb_private::Stream *s,
                                   const ELFSectionHeaderInfo &sh);

  static void DumpELFSectionHeader_sh_type(lldb_private::Stream *s,
                                           elf::elf_word sh_type);

  static void DumpELFSectionHeader_sh_flags(lldb_private::Stream *s,
                                            elf::elf_xword sh_flags);
  //@}

  /// ELF dependent module dump routine.
  void DumpDependentModules(lldb_private::Stream *s);

  const elf::ELFDynamic *FindDynamicSymbol(unsigned tag);

  unsigned PLTRelocationType();

  static lldb_private::Status
  RefineModuleDetailsFromNote(lldb_private::DataExtractor &data,
                              lldb_private::ArchSpec &arch_spec,
                              lldb_private::UUID &uuid);

  bool AnySegmentHasPhysicalAddress();
};

#endif // liblldb_ObjectFileELF_h_
