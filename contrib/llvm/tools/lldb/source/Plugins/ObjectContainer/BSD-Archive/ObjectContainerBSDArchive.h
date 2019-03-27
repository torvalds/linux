//===-- ObjectContainerBSDArchive.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ObjectContainerBSDArchive_h_
#define liblldb_ObjectContainerBSDArchive_h_

#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Symbol/ObjectContainer.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"

#include "llvm/Support/Chrono.h"

#include <map>
#include <memory>
#include <mutex>

class ObjectContainerBSDArchive : public lldb_private::ObjectContainer {
public:
  ObjectContainerBSDArchive(const lldb::ModuleSP &module_sp,
                            lldb::DataBufferSP &data_sp,
                            lldb::offset_t data_offset,
                            const lldb_private::FileSpec *file,
                            lldb::offset_t offset, lldb::offset_t length);

  ~ObjectContainerBSDArchive() override;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  static lldb_private::ObjectContainer *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                 lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                 lldb::offset_t offset, lldb::offset_t length);

  static size_t GetModuleSpecifications(const lldb_private::FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        lldb_private::ModuleSpecList &specs);

  static bool MagicBytesMatch(const lldb_private::DataExtractor &data);

  //------------------------------------------------------------------
  // Member Functions
  //------------------------------------------------------------------
  bool ParseHeader() override;

  size_t GetNumObjects() const override {
    if (m_archive_sp)
      return m_archive_sp->GetNumObjects();
    return 0;
  }

  void Dump(lldb_private::Stream *s) const override;

  lldb::ObjectFileSP GetObjectFile(const lldb_private::FileSpec *file) override;

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

protected:
  struct Object {
    Object();

    void Clear();

    lldb::offset_t Extract(const lldb_private::DataExtractor &data,
                           lldb::offset_t offset);

    lldb_private::ConstString ar_name; // name
    uint32_t ar_date;                  // modification time
    uint16_t ar_uid;                   // user id
    uint16_t ar_gid;                   // group id
    uint16_t ar_mode;                  // octal file permissions
    uint32_t ar_size;                  // size in bytes
    lldb::offset_t ar_file_offset; // file offset in bytes from the beginning of
                                   // the file of the object data
    lldb::offset_t ar_file_size;   // length of the object data

    typedef std::vector<Object> collection;
    typedef collection::iterator iterator;
    typedef collection::const_iterator const_iterator;
  };

  class Archive {
  public:
    typedef std::shared_ptr<Archive> shared_ptr;
    typedef std::multimap<lldb_private::FileSpec, shared_ptr> Map;

    Archive(const lldb_private::ArchSpec &arch,
            const llvm::sys::TimePoint<> &mod_time, lldb::offset_t file_offset,
            lldb_private::DataExtractor &data);

    ~Archive();

    static Map &GetArchiveCache();

    static std::recursive_mutex &GetArchiveCacheMutex();

    static Archive::shared_ptr FindCachedArchive(
        const lldb_private::FileSpec &file, const lldb_private::ArchSpec &arch,
        const llvm::sys::TimePoint<> &mod_time, lldb::offset_t file_offset);

    static Archive::shared_ptr ParseAndCacheArchiveForFile(
        const lldb_private::FileSpec &file, const lldb_private::ArchSpec &arch,
        const llvm::sys::TimePoint<> &mod_time, lldb::offset_t file_offset,
        lldb_private::DataExtractor &data);

    size_t GetNumObjects() const { return m_objects.size(); }

    const Object *GetObjectAtIndex(size_t idx) {
      if (idx < m_objects.size())
        return &m_objects[idx];
      return NULL;
    }

    size_t ParseObjects();

    Object *FindObject(const lldb_private::ConstString &object_name,
                       const llvm::sys::TimePoint<> &object_mod_time);

    lldb::offset_t GetFileOffset() const { return m_file_offset; }

    const llvm::sys::TimePoint<> &GetModificationTime() { return m_time; }

    const lldb_private::ArchSpec &GetArchitecture() const { return m_arch; }

    void SetArchitecture(const lldb_private::ArchSpec &arch) { m_arch = arch; }

    bool HasNoExternalReferences() const;

    lldb_private::DataExtractor &GetData() { return m_data; }

  protected:
    typedef lldb_private::UniqueCStringMap<uint32_t> ObjectNameToIndexMap;
    //----------------------------------------------------------------------
    // Member Variables
    //----------------------------------------------------------------------
    lldb_private::ArchSpec m_arch;
    llvm::sys::TimePoint<> m_time;
    lldb::offset_t m_file_offset;
    Object::collection m_objects;
    ObjectNameToIndexMap m_object_name_to_index_map;
    lldb_private::DataExtractor m_data; ///< The data for this object container
                                        ///so we don't lose data if the .a files
                                        ///gets modified
  };

  void SetArchive(Archive::shared_ptr &archive_sp);

  Archive::shared_ptr m_archive_sp;
};

#endif // liblldb_ObjectContainerBSDArchive_h_
