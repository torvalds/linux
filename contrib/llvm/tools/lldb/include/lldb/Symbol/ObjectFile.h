//===-- ObjectFile.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ObjectFile_h_
#define liblldb_ObjectFile_h_

#include "lldb/Core/FileSpecList.h"
#include "lldb/Core/ModuleChild.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/UnwindTable.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-private.h"
#include "llvm/Support/VersionTuple.h"

namespace lldb_private {

class ObjectFileJITDelegate {
public:
  ObjectFileJITDelegate() {}

  virtual ~ObjectFileJITDelegate() {}

  virtual lldb::ByteOrder GetByteOrder() const = 0;

  virtual uint32_t GetAddressByteSize() const = 0;

  virtual void PopulateSymtab(lldb_private::ObjectFile *obj_file,
                              lldb_private::Symtab &symtab) = 0;

  virtual void PopulateSectionList(lldb_private::ObjectFile *obj_file,
                                   lldb_private::SectionList &section_list) = 0;

  virtual ArchSpec GetArchitecture() = 0;
};

//----------------------------------------------------------------------
/// @class ObjectFile ObjectFile.h "lldb/Symbol/ObjectFile.h"
/// A plug-in interface definition class for object file parsers.
///
/// Object files belong to Module objects and know how to extract information
/// from executable, shared library, and object (.o) files used by operating
/// system runtime. The symbol table and section list for an object file.
///
/// Object files can be represented by the entire file, or by part of a file.
/// An example of a partial file ObjectFile is one that contains information
/// for one of multiple architectures in the same file.
///
/// Once an architecture is selected the object file information can be
/// extracted from this abstract class.
//----------------------------------------------------------------------
class ObjectFile : public std::enable_shared_from_this<ObjectFile>,
                   public PluginInterface,
                   public ModuleChild {
  friend class lldb_private::Module;

public:
  typedef enum {
    eTypeInvalid = 0,
    eTypeCoreFile,      /// A core file that has a checkpoint of a program's
                        /// execution state
    eTypeExecutable,    /// A normal executable
    eTypeDebugInfo,     /// An object file that contains only debug information
    eTypeDynamicLinker, /// The platform's dynamic linker executable
    eTypeObjectFile,    /// An intermediate object file
    eTypeSharedLibrary, /// A shared library that can be used during execution
    eTypeStubLibrary, /// A library that can be linked against but not used for
                      /// execution
    eTypeJIT, /// JIT code that has symbols, sections and possibly debug info
    eTypeUnknown
  } Type;

  typedef enum {
    eStrataInvalid = 0,
    eStrataUnknown,
    eStrataUser,
    eStrataKernel,
    eStrataRawImage,
    eStrataJIT
  } Strata;

  struct LoadableData {
    lldb::addr_t Dest;
    llvm::ArrayRef<uint8_t> Contents;
  };

  //------------------------------------------------------------------
  /// Construct with a parent module, offset, and header data.
  ///
  /// Object files belong to modules and a valid module must be supplied upon
  /// construction. The at an offset within a file for objects that contain
  /// more than one architecture or object.
  //------------------------------------------------------------------
  ObjectFile(const lldb::ModuleSP &module_sp, const FileSpec *file_spec_ptr,
             lldb::offset_t file_offset, lldb::offset_t length,
             const lldb::DataBufferSP &data_sp, lldb::offset_t data_offset);

  ObjectFile(const lldb::ModuleSP &module_sp, const lldb::ProcessSP &process_sp,
             lldb::addr_t header_addr, lldb::DataBufferSP &data_sp);

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// The destructor is virtual since this class is designed to be inherited
  /// from by the plug-in instance.
  //------------------------------------------------------------------
  ~ObjectFile() override;

  //------------------------------------------------------------------
  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the current contents of this object to the
  /// supplied stream \a s. The dumping should include the section list if it
  /// has been parsed, and the symbol table if it has been parsed.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  //------------------------------------------------------------------
  virtual void Dump(Stream *s) = 0;

  //------------------------------------------------------------------
  /// Find a ObjectFile plug-in that can parse \a file_spec.
  ///
  /// Scans all loaded plug-in interfaces that implement versions of the
  /// ObjectFile plug-in interface and returns the first instance that can
  /// parse the file.
  ///
  /// @param[in] module
  ///     The parent module that owns this object file.
  ///
  /// @param[in] file_spec
  ///     A file specification that indicates which file to use as the
  ///     object file.
  ///
  /// @param[in] file_offset
  ///     The offset into the file at which to start parsing the
  ///     object. This is for files that contain multiple
  ///     architectures or objects.
  ///
  /// @param[in] file_size
  ///     The size of the current object file if it can be determined
  ///     or if it is known. This can be zero.
  ///
  /// @see ObjectFile::ParseHeader()
  //------------------------------------------------------------------
  static lldb::ObjectFileSP
  FindPlugin(const lldb::ModuleSP &module_sp, const FileSpec *file_spec,
             lldb::offset_t file_offset, lldb::offset_t file_size,
             lldb::DataBufferSP &data_sp, lldb::offset_t &data_offset);

  //------------------------------------------------------------------
  /// Find a ObjectFile plug-in that can parse a file in memory.
  ///
  /// Scans all loaded plug-in interfaces that implement versions of the
  /// ObjectFile plug-in interface and returns the first instance that can
  /// parse the file.
  ///
  /// @param[in] module
  ///     The parent module that owns this object file.
  ///
  /// @param[in] process_sp
  ///     A shared pointer to the process whose memory space contains
  ///     an object file. This will be stored as a std::weak_ptr.
  ///
  /// @param[in] header_addr
  ///     The address of the header for the object file in memory.
  //------------------------------------------------------------------
  static lldb::ObjectFileSP FindPlugin(const lldb::ModuleSP &module_sp,
                                       const lldb::ProcessSP &process_sp,
                                       lldb::addr_t header_addr,
                                       lldb::DataBufferSP &file_data_sp);

  static size_t GetModuleSpecifications(const FileSpec &file,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t file_size,
                                        ModuleSpecList &specs);

  static size_t GetModuleSpecifications(const lldb_private::FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t file_size,
                                        lldb_private::ModuleSpecList &specs);
  //------------------------------------------------------------------
  /// Split a path into a file path with object name.
  ///
  /// For paths like "/tmp/foo.a(bar.o)" we often need to split a path up into
  /// the actual path name and into the object name so we can make a valid
  /// object file from it.
  ///
  /// @param[in] path_with_object
  ///     A path that might contain an archive path with a .o file
  ///     specified in parens in the basename of the path.
  ///
  /// @param[out] archive_file
  ///     If \b true is returned, \a file_spec will be filled in with
  ///     the path to the archive.
  ///
  /// @param[out] archive_object
  ///     If \b true is returned, \a object will be filled in with
  ///     the name of the object inside the archive.
  ///
  /// @return
  ///     \b true if the path matches the pattern of archive + object
  ///     and \a archive_file and \a archive_object are modified,
  ///     \b false otherwise and \a archive_file and \a archive_object
  ///     are guaranteed to be remain unchanged.
  //------------------------------------------------------------------
  static bool SplitArchivePathWithObject(
      const char *path_with_object, lldb_private::FileSpec &archive_file,
      lldb_private::ConstString &archive_object, bool must_exist);

  //------------------------------------------------------------------
  /// Gets the address size in bytes for the current object file.
  ///
  /// @return
  ///     The size of an address in bytes for the currently selected
  ///     architecture (and object for archives). Returns zero if no
  ///     architecture or object has been selected.
  //------------------------------------------------------------------
  virtual uint32_t GetAddressByteSize() const = 0;

  //------------------------------------------------------------------
  /// Get the address type given a file address in an object file.
  ///
  /// Many binary file formats know what kinds This is primarily for ARM
  /// binaries, though it can be applied to any executable file format that
  /// supports different opcode types within the same binary. ARM binaries
  /// support having both ARM and Thumb within the same executable container.
  /// We need to be able to get @return
  ///     The size of an address in bytes for the currently selected
  ///     architecture (and object for archives). Returns zero if no
  ///     architecture or object has been selected.
  //------------------------------------------------------------------
  virtual AddressClass GetAddressClass(lldb::addr_t file_addr);

  //------------------------------------------------------------------
  /// Extract the dependent modules from an object file.
  ///
  /// If an object file has information about which other images it depends on
  /// (such as shared libraries), this function will provide the list. Since
  /// many executables or shared libraries may depend on the same files,
  /// FileSpecList::AppendIfUnique(const FileSpec &) should be used to make
  /// sure any files that are added are not already in the list.
  ///
  /// @param[out] file_list
  ///     A list of file specification objects that gets dependent
  ///     files appended to.
  ///
  /// @return
  ///     The number of new files that were appended to \a file_list.
  ///
  /// @see FileSpecList::AppendIfUnique(const FileSpec &)
  //------------------------------------------------------------------
  virtual uint32_t GetDependentModules(FileSpecList &file_list) = 0;

  //------------------------------------------------------------------
  /// Tells whether this object file is capable of being the main executable
  /// for a process.
  ///
  /// @return
  ///     \b true if it is, \b false otherwise.
  //------------------------------------------------------------------
  virtual bool IsExecutable() const = 0;

  //------------------------------------------------------------------
  /// Returns the offset into a file at which this object resides.
  ///
  /// Some files contain many object files, and this function allows access to
  /// an object's offset within the file.
  ///
  /// @return
  ///     The offset in bytes into the file. Defaults to zero for
  ///     simple object files that a represented by an entire file.
  //------------------------------------------------------------------
  virtual lldb::addr_t GetFileOffset() const { return m_file_offset; }

  virtual lldb::addr_t GetByteSize() const { return m_length; }

  //------------------------------------------------------------------
  /// Get accessor to the object file specification.
  ///
  /// @return
  ///     The file specification object pointer if there is one, or
  ///     NULL if this object is only from memory.
  //------------------------------------------------------------------
  virtual FileSpec &GetFileSpec() { return m_file; }

  //------------------------------------------------------------------
  /// Get const accessor to the object file specification.
  ///
  /// @return
  ///     The const file specification object pointer if there is one,
  ///     or NULL if this object is only from memory.
  //------------------------------------------------------------------
  virtual const FileSpec &GetFileSpec() const { return m_file; }

  //------------------------------------------------------------------
  /// Get the ArchSpec for this object file.
  ///
  /// @return
  ///     The ArchSpec of this object file. In case of error, an invalid
  ///     ArchSpec object is returned.
  //------------------------------------------------------------------
  virtual ArchSpec GetArchitecture() = 0;

  //------------------------------------------------------------------
  /// Gets the section list for the currently selected architecture (and
  /// object for archives).
  ///
  /// Section list parsing can be deferred by ObjectFile instances until this
  /// accessor is called the first time.
  ///
  /// @return
  ///     The list of sections contained in this object file.
  //------------------------------------------------------------------
  virtual SectionList *GetSectionList(bool update_module_section_list = true);

  virtual void CreateSections(SectionList &unified_section_list) = 0;

  //------------------------------------------------------------------
  /// Notify the ObjectFile that the file addresses in the Sections for this
  /// module have been changed.
  //------------------------------------------------------------------
  virtual void SectionFileAddressesChanged() {}

  //------------------------------------------------------------------
  /// Gets the symbol table for the currently selected architecture (and
  /// object for archives).
  ///
  /// Symbol table parsing can be deferred by ObjectFile instances until this
  /// accessor is called the first time.
  ///
  /// @return
  ///     The symbol table for this object file.
  //------------------------------------------------------------------
  virtual Symtab *GetSymtab() = 0;

  //------------------------------------------------------------------
  /// Perform relocations on the section if necessary.
  ///
  //------------------------------------------------------------------
  virtual void RelocateSection(lldb_private::Section *section);

  //------------------------------------------------------------------
  /// Appends a Symbol for the specified so_addr to the symbol table.
  ///
  /// If verify_unique is false, the symbol table is not searched to determine
  /// if a Symbol found at this address has already been added to the symbol
  /// table.  When verify_unique is true, this method resolves the Symbol as
  /// the first match in the SymbolTable and appends a Symbol only if
  /// required/found.
  ///
  /// @return
  ///     The resolved symbol or nullptr.  Returns nullptr if a
  ///     a Symbol could not be found for the specified so_addr.
  //------------------------------------------------------------------
  virtual Symbol *ResolveSymbolForAddress(const Address &so_addr,
                                          bool verify_unique) {
    // Typically overridden to lazily add stripped symbols recoverable from the
    // exception handling unwind information (i.e. without parsing the entire
    // eh_frame section.
    //
    // The availability of LC_FUNCTION_STARTS allows ObjectFileMachO to
    // efficiently add stripped symbols when the symbol table is first
    // constructed.  Poorer cousins are PECoff and ELF.
    return nullptr;
  }

  //------------------------------------------------------------------
  /// Detect if this object file has been stripped of local symbols.
  //------------------------------------------------------------------
  /// Detect if this object file has been stripped of local symbols.
  ///
  /// @return
  ///     Return \b true if the object file has been stripped of local
  ///     symbols.
  //------------------------------------------------------------------
  virtual bool IsStripped() = 0;

  //------------------------------------------------------------------
  /// Frees the symbol table.
  ///
  /// This function should only be used when an object file is
  ///
  /// @param[in] flags
  ///     eSymtabFromUnifiedSectionList: Whether to clear symbol table
  ///     for unified module section list, or object file.
  ///
  /// @return
  ///     The symbol table for this object file.
  //------------------------------------------------------------------
  virtual void ClearSymtab();

  //------------------------------------------------------------------
  /// Gets the UUID for this object file.
  ///
  /// If the object file format contains a UUID, the value should be returned.
  /// Else ObjectFile instances should return the MD5 checksum of all of the
  /// bytes for the object file (or memory for memory based object files).
  ///
  /// @return
  ///     Returns \b true if a UUID was successfully extracted into
  ///     \a uuid, \b false otherwise.
  //------------------------------------------------------------------
  virtual bool GetUUID(lldb_private::UUID *uuid) = 0;

  //------------------------------------------------------------------
  /// Gets the symbol file spec list for this object file.
  ///
  /// If the object file format contains a debug symbol file link, the values
  /// will be returned in the FileSpecList.
  ///
  /// @return
  ///     Returns filespeclist.
  //------------------------------------------------------------------
  virtual lldb_private::FileSpecList GetDebugSymbolFilePaths() {
    return FileSpecList();
  }

  //------------------------------------------------------------------
  /// Gets the file spec list of libraries re-exported by this object file.
  ///
  /// If the object file format has the notion of one library re-exporting the
  /// symbols from another, the re-exported libraries will be returned in the
  /// FileSpecList.
  ///
  /// @return
  ///     Returns filespeclist.
  //------------------------------------------------------------------
  virtual lldb_private::FileSpecList GetReExportedLibraries() {
    return FileSpecList();
  }

  //------------------------------------------------------------------
  /// Sets the load address for an entire module, assuming a rigid slide of
  /// sections, if possible in the implementation.
  ///
  /// @return
  ///     Returns true iff any section's load address changed.
  //------------------------------------------------------------------
  virtual bool SetLoadAddress(Target &target, lldb::addr_t value,
                              bool value_is_offset) {
    return false;
  }

  //------------------------------------------------------------------
  /// Gets whether endian swapping should occur when extracting data from this
  /// object file.
  ///
  /// @return
  ///     Returns \b true if endian swapping is needed, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  virtual lldb::ByteOrder GetByteOrder() const = 0;

  //------------------------------------------------------------------
  /// Attempts to parse the object header.
  ///
  /// This function is used as a test to see if a given plug-in instance can
  /// parse the header data already contained in ObjectFile::m_data. If an
  /// object file parser does not recognize that magic bytes in a header,
  /// false should be returned and the next plug-in can attempt to parse an
  /// object file.
  ///
  /// @return
  ///     Returns \b true if the header was parsed successfully, \b
  ///     false otherwise.
  //------------------------------------------------------------------
  virtual bool ParseHeader() = 0;

  //------------------------------------------------------------------
  /// Returns a reference to the UnwindTable for this ObjectFile
  ///
  /// The UnwindTable contains FuncUnwinders objects for any function in this
  /// ObjectFile.  If a FuncUnwinders object hasn't been created yet (i.e. the
  /// function has yet to be unwound in a stack walk), it will be created when
  /// requested.  Specifically, we do not create FuncUnwinders objects for
  /// functions until they are needed.
  ///
  /// @return
  ///     Returns the unwind table for this object file.
  //------------------------------------------------------------------
  virtual lldb_private::UnwindTable &GetUnwindTable() { return m_unwind_table; }

  //------------------------------------------------------------------
  /// Returns if the function bounds for symbols in this symbol file are
  /// likely accurate.
  ///
  /// The unwinder can emulate the instructions of functions to understand
  /// prologue/epilogue code sequences, where registers are spilled on the
  /// stack, etc.  This feature relies on having the correct start addresses
  /// of all functions.  If the ObjectFile has a way to tell that symbols have
  /// been stripped and there's no way to reconstruct start addresses (e.g.
  /// LC_FUNCTION_STARTS on Mach-O, or eh_frame unwind info), the ObjectFile
  /// should indicate that assembly emulation should not be used for this
  /// module.
  ///
  /// It is uncommon for this to return false.  An ObjectFile needs to be sure
  /// that symbol start addresses are unavailable before false is returned.
  /// If it is unclear, this should return true.
  ///
  /// @return
  ///     Returns true if assembly emulation should be used for this
  ///     module.
  ///     Only returns false if the ObjectFile is sure that symbol
  ///     addresses are insufficient for accurate assembly emulation.
  //------------------------------------------------------------------
  virtual bool AllowAssemblyEmulationUnwindPlans() { return true; }

  //------------------------------------------------------------------
  /// Similar to Process::GetImageInfoAddress().
  ///
  /// Some platforms embed auxiliary structures useful to debuggers in the
  /// address space of the inferior process.  This method returns the address
  /// of such a structure if the information can be resolved via entries in
  /// the object file.  ELF, for example, provides a means to hook into the
  /// runtime linker so that a debugger may monitor the loading and unloading
  /// of shared libraries.
  ///
  /// @return
  ///     The address of any auxiliary tables, or an invalid address if this
  ///     object file format does not support or contain such information.
  virtual lldb_private::Address GetImageInfoAddress(Target *target) {
    return Address();
  }

  //------------------------------------------------------------------
  /// Returns the address of the Entry Point in this object file - if the
  /// object file doesn't have an entry point (because it is not an executable
  /// file) then an invalid address is returned.
  ///
  /// @return
  ///     Returns the entry address for this module.
  //------------------------------------------------------------------
  virtual lldb_private::Address GetEntryPointAddress() { return Address(); }

  //------------------------------------------------------------------
  /// Returns base address of this object file.
  ///
  /// This also sometimes referred to as the "preferred load address" or the
  /// "image base address". Addresses within object files are often expressed
  /// relative to this base. If this address corresponds to a specific section
  /// (usually the first byte of the first section) then the returned address
  /// will have this section set. Otherwise, the address will just have the
  /// offset member filled in, indicating that this represents a file address.
  //------------------------------------------------------------------
  virtual lldb_private::Address GetBaseAddress() {
    return Address(m_memory_addr);
  }

  virtual uint32_t GetNumThreadContexts() { return 0; }

  //------------------------------------------------------------------
  /// Some object files may have an identifier string embedded in them, e.g.
  /// in a Mach-O core file using the LC_IDENT load command (which  is
  /// obsolete, but can still be found in some old files)
  ///
  /// @return
  ///     Returns the identifier string if one exists, else an empty
  ///     string.
  //------------------------------------------------------------------
  virtual std::string GetIdentifierString () { 
      return std::string(); 
  }

  //------------------------------------------------------------------
  /// When the ObjectFile is a core file, lldb needs to locate the "binary" in
  /// the core file.  lldb can iterate over the pages looking for a valid
  /// binary, but some core files may have metadata  describing where the main
  /// binary is exactly which removes ambiguity when there are multiple
  /// binaries present in the captured memory pages.
  ///
  /// @param[out] address
  ///   If the address of the binary is specified, this will be set.
  ///   This is an address is the virtual address space of the core file
  ///   memory segments; it is not an offset into the object file.
  ///   If no address is available, will be set to LLDB_INVALID_ADDRESS.
  ///
  /// @param[out] uuid
  ///   If the uuid of the binary is specified, this will be set.
  ///   If no UUID is available, will be cleared.
  ///
  /// @return
  ///   Returns true if either address or uuid has been set.
  //------------------------------------------------------------------
  virtual bool GetCorefileMainBinaryInfo (lldb::addr_t &address, UUID &uuid) {
      address = LLDB_INVALID_ADDRESS;
      uuid.Clear();
      return false;
  }

  virtual lldb::RegisterContextSP
  GetThreadContextAtIndex(uint32_t idx, lldb_private::Thread &thread) {
    return lldb::RegisterContextSP();
  }

  //------------------------------------------------------------------
  /// The object file should be able to calculate its type by looking at its
  /// file header and possibly the sections or other data in the object file.
  /// The file type is used in the debugger to help select the correct plug-
  /// ins for the job at hand, so this is important to get right. If any
  /// eTypeXXX definitions do not match up with the type of file you are
  /// loading, please feel free to add a new enumeration value.
  ///
  /// @return
  ///     The calculated file type for the current object file.
  //------------------------------------------------------------------
  virtual Type CalculateType() = 0;

  //------------------------------------------------------------------
  /// In cases where the type can't be calculated (elf files), this routine
  /// allows someone to explicitly set it. As an example, SymbolVendorELF uses
  /// this routine to set eTypeDebugInfo when loading debug link files.
  virtual void SetType(Type type) { m_type = type; }

  //------------------------------------------------------------------
  /// The object file should be able to calculate the strata of the object
  /// file.
  ///
  /// Many object files for platforms might be for either user space debugging
  /// or for kernel debugging. If your object file subclass can figure this
  /// out, it will help with debugger plug-in selection when it comes time to
  /// debug.
  ///
  /// @return
  ///     The calculated object file strata for the current object
  ///     file.
  //------------------------------------------------------------------
  virtual Strata CalculateStrata() = 0;

  //------------------------------------------------------------------
  /// Get the object file version numbers.
  ///
  /// Many object files have a set of version numbers that describe the
  /// version of the executable or shared library. Typically there are major,
  /// minor and build, but there may be more. This function will extract the
  /// versions from object files if they are available.
  ///
  /// @return
  ///     This function returns extracted version numbers as a
  ///     llvm::VersionTuple. In case of error an empty VersionTuple is
  ///     returned.
  //------------------------------------------------------------------
  virtual llvm::VersionTuple GetVersion() { return llvm::VersionTuple(); }

  //------------------------------------------------------------------
  /// Get the minimum OS version this object file can run on.
  ///
  /// Some object files have information that specifies the minimum OS version
  /// that they can be used on.
  ///
  /// @return
  ///     This function returns extracted version numbers as a
  ///     llvm::VersionTuple. In case of error an empty VersionTuple is
  ///     returned.
  //------------------------------------------------------------------
  virtual llvm::VersionTuple GetMinimumOSVersion() {
    return llvm::VersionTuple();
  }

  //------------------------------------------------------------------
  /// Get the SDK OS version this object file was built with.
  ///
  /// The versions arguments and returns values are the same as the
  /// GetMinimumOSVersion()
  //------------------------------------------------------------------
  virtual uint32_t GetSDKVersion(uint32_t *versions, uint32_t num_versions) {
    if (versions && num_versions) {
      for (uint32_t i = 0; i < num_versions; ++i)
        versions[i] = UINT32_MAX;
    }
    return 0;
  }

  //------------------------------------------------------------------
  /// Return true if this file is a dynamic link editor (dyld)
  ///
  /// Often times dyld has symbols that mirror symbols in libc and other
  /// shared libraries (like "malloc" and "free") and the user does _not_ want
  /// to stop in these shared libraries by default. We can ask the ObjectFile
  /// if it is such a file and should be avoided for things like settings
  /// breakpoints and doing function lookups for expressions.
  //------------------------------------------------------------------
  virtual bool GetIsDynamicLinkEditor() { return false; }

  //------------------------------------------------------------------
  // Member Functions
  //------------------------------------------------------------------
  Type GetType() {
    if (m_type == eTypeInvalid)
      m_type = CalculateType();
    return m_type;
  }

  Strata GetStrata() {
    if (m_strata == eStrataInvalid)
      m_strata = CalculateStrata();
    return m_strata;
  }

  // When an object file is in memory, subclasses should try and lock the
  // process weak pointer. If the process weak pointer produces a valid
  // ProcessSP, then subclasses can call this function to read memory.
  static lldb::DataBufferSP ReadMemory(const lldb::ProcessSP &process_sp,
                                       lldb::addr_t addr, size_t byte_size);

  // This function returns raw file contents. Do not use it if you want
  // transparent decompression of section contents.
  size_t GetData(lldb::offset_t offset, size_t length,
                 DataExtractor &data) const;

  // This function returns raw file contents. Do not use it if you want
  // transparent decompression of section contents.
  size_t CopyData(lldb::offset_t offset, size_t length, void *dst) const;

  // This function will transparently decompress section data if the section if
  // compressed.
  virtual size_t ReadSectionData(Section *section,
                                 lldb::offset_t section_offset, void *dst,
                                 size_t dst_len);

  // This function will transparently decompress section data if the section if
  // compressed. Note that for compressed section the resulting data size may
  // be larger than what Section::GetFileSize reports.
  virtual size_t ReadSectionData(Section *section,
                                 DataExtractor &section_data);

  bool IsInMemory() const { return m_memory_addr != LLDB_INVALID_ADDRESS; }

  // Strip linker annotations (such as @@VERSION) from symbol names.
  virtual llvm::StringRef
  StripLinkerSymbolAnnotations(llvm::StringRef symbol_name) const {
    return symbol_name;
  }

  static lldb::SymbolType GetSymbolTypeFromName(
      llvm::StringRef name,
      lldb::SymbolType symbol_type_hint = lldb::eSymbolTypeUndefined);

  //------------------------------------------------------------------
  /// Loads this objfile to memory.
  ///
  /// Loads the bits needed to create an executable image to the memory. It is
  /// useful with bare-metal targets where target does not have the ability to
  /// start a process itself.
  ///
  /// @param[in] target
  ///     Target where to load.
  ///
  /// @return
  //------------------------------------------------------------------
  virtual std::vector<LoadableData> GetLoadableData(Target &target);

protected:
  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  FileSpec m_file;
  Type m_type;
  Strata m_strata;
  lldb::addr_t m_file_offset; ///< The offset in bytes into the file, or the
                              ///address in memory
  lldb::addr_t m_length; ///< The length of this object file if it is known (can
                         ///be zero if length is unknown or can't be
                         ///determined).
  DataExtractor
      m_data; ///< The data for this object file so things can be parsed lazily.
  lldb_private::UnwindTable m_unwind_table; /// < Table of FuncUnwinders objects
                                            /// created for this ObjectFile's
                                            /// functions
  lldb::ProcessWP m_process_wp;
  const lldb::addr_t m_memory_addr;
  std::unique_ptr<lldb_private::SectionList> m_sections_ap;
  std::unique_ptr<lldb_private::Symtab> m_symtab_ap;
  uint32_t m_synthetic_symbol_idx;

  //------------------------------------------------------------------
  /// Sets the architecture for a module.  At present the architecture can
  /// only be set if it is invalid.  It is not allowed to switch from one
  /// concrete architecture to another.
  ///
  /// @param[in] new_arch
  ///     The architecture this module will be set to.
  ///
  /// @return
  ///     Returns \b true if the architecture was changed, \b
  ///     false otherwise.
  //------------------------------------------------------------------
  bool SetModulesArchitecture(const ArchSpec &new_arch);

  ConstString GetNextSyntheticSymbolName();

  static lldb::DataBufferSP MapFileData(const FileSpec &file, uint64_t Size,
                                        uint64_t Offset);

private:
  DISALLOW_COPY_AND_ASSIGN(ObjectFile);
};

} // namespace lldb_private

namespace llvm {
template <> struct format_provider<lldb_private::ObjectFile::Type> {
  static void format(const lldb_private::ObjectFile::Type &type,
                     raw_ostream &OS, StringRef Style);
};

template <> struct format_provider<lldb_private::ObjectFile::Strata> {
  static void format(const lldb_private::ObjectFile::Strata &strata,
                     raw_ostream &OS, StringRef Style);
};
} // namespace llvm

#endif // liblldb_ObjectFile_h_
