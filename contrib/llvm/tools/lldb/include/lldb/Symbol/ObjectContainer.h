//===-- ObjectContainer.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ObjectContainer_h_
#define liblldb_ObjectContainer_h_

#include "lldb/Core/ModuleChild.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class ObjectContainer ObjectContainer.h "lldb/Symbol/ObjectContainer.h"
/// A plug-in interface definition class for object containers.
///
/// Object containers contain object files from one or more architectures, and
/// also can contain one or more named objects.
///
/// Typical object containers are static libraries (.a files) that contain
/// multiple named object files, and universal files that contain multiple
/// architectures.
//----------------------------------------------------------------------
class ObjectContainer : public PluginInterface, public ModuleChild {
public:
  //------------------------------------------------------------------
  /// Construct with a parent module, offset, and header data.
  ///
  /// Object files belong to modules and a valid module must be supplied upon
  /// construction. The at an offset within a file for objects that contain
  /// more than one architecture or object.
  //------------------------------------------------------------------
  ObjectContainer(const lldb::ModuleSP &module_sp, const FileSpec *file,
                  lldb::offset_t file_offset, lldb::offset_t length,
                  lldb::DataBufferSP &data_sp, lldb::offset_t data_offset)
      : ModuleChild(module_sp),
        m_file(), // This file can be different than the module's file spec
        m_offset(file_offset), m_length(length), m_data() {
    if (file)
      m_file = *file;
    if (data_sp)
      m_data.SetData(data_sp, data_offset, length);
  }

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// The destructor is virtual since this class is designed to be inherited
  /// from by the plug-in instance.
  //------------------------------------------------------------------
  ~ObjectContainer() override = default;

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
  virtual void Dump(Stream *s) const = 0;

  //------------------------------------------------------------------
  /// Gets the architecture given an index.
  ///
  /// Copies the architecture specification for index \a idx.
  ///
  /// @param[in] idx
  ///     The architecture index to extract.
  ///
  /// @param[out] arch
  ///     A architecture object that will be filled in if \a idx is a
  ///     architecture valid index.
  ///
  /// @return
  ///     Returns \b true if \a idx is valid and \a arch has been
  ///     filled in, \b false otherwise.
  ///
  /// @see ObjectContainer::GetNumArchitectures() const
  //------------------------------------------------------------------
  virtual bool GetArchitectureAtIndex(uint32_t idx, ArchSpec &arch) const {
    return false;
  }

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
  virtual lldb::addr_t GetOffset() const { return m_offset; }

  virtual lldb::addr_t GetByteSize() const { return m_length; }

  //------------------------------------------------------------------
  /// Get the number of objects within this object file (archives).
  ///
  /// @return
  ///     Zero for object files that are not archives, or the number
  ///     of objects contained in the archive.
  //------------------------------------------------------------------
  virtual size_t GetNumObjects() const { return 0; }

  //------------------------------------------------------------------
  /// Get the number of architectures in this object file.
  ///
  /// The default implementation returns 1 as for object files that contain a
  /// single architecture. ObjectContainer instances that contain more than
  /// one architecture should override this function and return an appropriate
  /// value.
  ///
  /// @return
  ///     The number of architectures contained in this object file.
  //------------------------------------------------------------------
  virtual size_t GetNumArchitectures() const { return 0; }

  //------------------------------------------------------------------
  /// Attempts to parse the object header.
  ///
  /// This function is used as a test to see if a given plug-in instance can
  /// parse the header data already contained in ObjectContainer::m_data. If
  /// an object file parser does not recognize that magic bytes in a header,
  /// false should be returned and the next plug-in can attempt to parse an
  /// object file.
  ///
  /// @return
  ///     Returns \b true if the header was parsed successfully, \b
  ///     false otherwise.
  //------------------------------------------------------------------
  virtual bool ParseHeader() = 0;

  //------------------------------------------------------------------
  /// Selects an architecture in an object file.
  ///
  /// Object files that contain a single architecture should verify that the
  /// specified \a arch matches the architecture in in object file and return
  /// \b true or \b false accordingly.
  ///
  /// Object files that contain more than one architecture should attempt to
  /// select that architecture, and if successful, clear out any previous
  /// state from any previously selected architecture and prepare to return
  /// information for the new architecture.
  ///
  /// @return
  ///     Returns a pointer to the object file of the requested \a
  ///     arch and optional \a name. Returns nullptr of no such object
  ///     file exists in the container.
  //------------------------------------------------------------------
  virtual lldb::ObjectFileSP GetObjectFile(const FileSpec *file) = 0;

  virtual bool ObjectAtIndexIsContainer(uint32_t object_idx) { return false; }

  virtual ObjectFile *GetObjectFileAtIndex(uint32_t object_idx) {
    return nullptr;
  }

  virtual ObjectContainer *GetObjectContainerAtIndex(uint32_t object_idx) {
    return nullptr;
  }

  virtual const char *GetObjectNameAtIndex(uint32_t object_idx) const {
    return nullptr;
  }

protected:
  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  FileSpec m_file; ///< The file that represents this container objects (which
                   ///can be different from the module's file).
  lldb::addr_t
      m_offset; ///< The offset in bytes into the file, or the address in memory
  lldb::addr_t m_length; ///< The size in bytes if known (can be zero).
  DataExtractor
      m_data; ///< The data for this object file so things can be parsed lazily.

private:
  DISALLOW_COPY_AND_ASSIGN(ObjectContainer);
};

} // namespace lldb_private

#endif // liblldb_ObjectContainer_h_
