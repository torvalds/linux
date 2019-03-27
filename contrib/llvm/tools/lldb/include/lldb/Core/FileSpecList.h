//===-- FileSpecList.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_FileSpecList_h_
#define liblldb_FileSpecList_h_
#if defined(__cplusplus)

#include "lldb/Utility/FileSpec.h"

#include <vector>

#include <stddef.h>

namespace lldb_private {
class Stream;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class FileSpecList FileSpecList.h "lldb/Core/FileSpecList.h"
/// A file collection class.
///
/// A class that contains a mutable list of FileSpec objects.
//----------------------------------------------------------------------
class FileSpecList {
public:
  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Initialize this object with an empty file list.
  //------------------------------------------------------------------
  FileSpecList();

  //------------------------------------------------------------------
  /// Copy constructor.
  ///
  /// Initialize this object with a copy of the file list from \a rhs.
  ///
  /// @param[in] rhs
  ///     A const reference to another file list object.
  //------------------------------------------------------------------
  FileSpecList(const FileSpecList &rhs);

  //------------------------------------------------------------------
  /// Destructor.
  //------------------------------------------------------------------
  ~FileSpecList();

  //------------------------------------------------------------------
  /// Assignment operator.
  ///
  /// Replace the file list in this object with the file list from \a rhs.
  ///
  /// @param[in] rhs
  ///     A file list object to copy.
  ///
  /// @return
  ///     A const reference to this object.
  //------------------------------------------------------------------
  const FileSpecList &operator=(const FileSpecList &rhs);

  //------------------------------------------------------------------
  /// Append a FileSpec object to the list.
  ///
  /// Appends \a file to the end of the file list.
  ///
  /// @param[in] file
  ///     A new file to append to this file list.
  //------------------------------------------------------------------
  void Append(const FileSpec &file);

  //------------------------------------------------------------------
  /// Append a FileSpec object if unique.
  ///
  /// Appends \a file to the end of the file list if it doesn't already exist
  /// in the file list.
  ///
  /// @param[in] file
  ///     A new file to append to this file list.
  ///
  /// @return
  ///     \b true if the file was appended, \b false otherwise.
  //------------------------------------------------------------------
  bool AppendIfUnique(const FileSpec &file);

  //------------------------------------------------------------------
  /// Clears the file list.
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  /// Dumps the file list to the supplied stream pointer "s".
  ///
  /// @param[in] s
  ///     The stream that will be used to dump the object description.
  //------------------------------------------------------------------
  void Dump(Stream *s, const char *separator_cstr = "\n") const;

  //------------------------------------------------------------------
  /// Find a file index.
  ///
  /// Find the index of the file in the file spec list that matches \a file
  /// starting \a idx entries into the file spec list.
  ///
  /// @param[in] idx
  ///     An index into the file list.
  ///
  /// @param[in] file
  ///     The file specification to search for.
  ///
  /// @param[in] full
  ///     Should FileSpec::Equal be called with "full" true or false.
  ///
  /// @return
  ///     The index of the file that matches \a file if it is found,
  ///     else UINT32_MAX is returned.
  //------------------------------------------------------------------
  size_t FindFileIndex(size_t idx, const FileSpec &file, bool full) const;

  //------------------------------------------------------------------
  /// Get file at index.
  ///
  /// Gets a file from the file list. If \a idx is not a valid index, an empty
  /// FileSpec object will be returned. The file objects that are returned can
  /// be tested using FileSpec::operator void*().
  ///
  /// @param[in] idx
  ///     An index into the file list.
  ///
  /// @return
  ///     A copy of the FileSpec object at index \a idx. If \a idx
  ///     is out of range, then an empty FileSpec object will be
  ///     returned.
  //------------------------------------------------------------------
  const FileSpec &GetFileSpecAtIndex(size_t idx) const;

  //------------------------------------------------------------------
  /// Get file specification pointer at index.
  ///
  /// Gets a file from the file list. The file objects that are returned can
  /// be tested using FileSpec::operator void*().
  ///
  /// @param[in] idx
  ///     An index into the file list.
  ///
  /// @return
  ///     A pointer to a contained FileSpec object at index \a idx.
  ///     If \a idx is out of range, then an NULL is returned.
  //------------------------------------------------------------------
  const FileSpec *GetFileSpecPointerAtIndex(size_t idx) const;

  //------------------------------------------------------------------
  /// Get the memory cost of this object.
  ///
  /// Return the size in bytes that this object takes in memory. This returns
  /// the size in bytes of this object, not any shared string values it may
  /// refer to.
  ///
  /// @return
  ///     The number of bytes that this object occupies in memory.
  ///
  /// @see ConstString::StaticMemorySize ()
  //------------------------------------------------------------------
  size_t MemorySize() const;

  bool IsEmpty() const { return m_files.empty(); }

  //------------------------------------------------------------------
  /// Get the number of files in the file list.
  ///
  /// @return
  ///     The number of files in the file spec list.
  //------------------------------------------------------------------
  size_t GetSize() const;

  bool Insert(size_t idx, const FileSpec &file) {
    if (idx < m_files.size()) {
      m_files.insert(m_files.begin() + idx, file);
      return true;
    } else if (idx == m_files.size()) {
      m_files.push_back(file);
      return true;
    }
    return false;
  }

  bool Replace(size_t idx, const FileSpec &file) {
    if (idx < m_files.size()) {
      m_files[idx] = file;
      return true;
    }
    return false;
  }

  bool Remove(size_t idx) {
    if (idx < m_files.size()) {
      m_files.erase(m_files.begin() + idx);
      return true;
    }
    return false;
  }

  static size_t GetFilesMatchingPartialPath(const char *path, bool dir_okay,
                                            FileSpecList &matches);

protected:
  typedef std::vector<FileSpec>
      collection;     ///< The collection type for the file list.
  collection m_files; ///< A collection of FileSpec objects.
};

} // namespace lldb_private

#endif // #if defined(__cplusplus)
#endif // liblldb_FileSpecList_h_
