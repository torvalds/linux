//===-- FileSpec.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_FileSpec_h_
#define liblldb_FileSpec_h_

#include <functional>
#include <string>

#include "lldb/Utility/ConstString.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class Stream;
}
namespace llvm {
class Triple;
}
namespace llvm {
class raw_ostream;
}
namespace llvm {
template <typename T> class SmallVectorImpl;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class FileSpec FileSpec.h "lldb/Host/FileSpec.h"
/// A file utility class.
///
/// A file specification class that divides paths up into a directory
/// and basename. These string values of the paths are put into uniqued string
/// pools for fast comparisons and efficient memory usage.
///
/// Another reason the paths are split into the directory and basename is to
/// allow efficient debugger searching. Often in a debugger the user types in
/// the basename of the file, for example setting a breakpoint by file and
/// line, or specifying a module (shared library) to limit the scope in which
/// to execute a command. The user rarely types in a full path. When the paths
/// are already split up, it makes it easy for us to compare only the
/// basenames of a lot of file specifications without having to split up the
/// file path each time to get to the basename.
//----------------------------------------------------------------------
class FileSpec {
public:
  using Style = llvm::sys::path::Style;

  FileSpec();

  //------------------------------------------------------------------
  /// Constructor with path.
  ///
  /// Takes a path to a file which can be just a filename, or a full path. If
  /// \a path is not nullptr or empty, this function will call
  /// FileSpec::SetFile (const char *path).
  ///
  /// @param[in] path
  ///     The full or partial path to a file.
  ///
  /// @param[in] style
  ///     The style of the path
  ///
  /// @see FileSpec::SetFile (const char *path)
  //------------------------------------------------------------------
  explicit FileSpec(llvm::StringRef path, Style style = Style::native);

  explicit FileSpec(llvm::StringRef path, const llvm::Triple &Triple);

  //------------------------------------------------------------------
  /// Copy constructor
  ///
  /// Makes a copy of the uniqued directory and filename strings from \a rhs.
  ///
  /// @param[in] rhs
  ///     A const FileSpec object reference to copy.
  //------------------------------------------------------------------
  FileSpec(const FileSpec &rhs);

  //------------------------------------------------------------------
  /// Copy constructor
  ///
  /// Makes a copy of the uniqued directory and filename strings from \a rhs
  /// if it is not nullptr.
  ///
  /// @param[in] rhs
  ///     A const FileSpec object pointer to copy if non-nullptr.
  //------------------------------------------------------------------
  FileSpec(const FileSpec *rhs);

  //------------------------------------------------------------------
  /// Destructor.
  //------------------------------------------------------------------
  ~FileSpec();

  bool DirectoryEquals(const FileSpec &other) const;

  bool FileEquals(const FileSpec &other) const;

  //------------------------------------------------------------------
  /// Assignment operator.
  ///
  /// Makes a copy of the uniqued directory and filename strings from \a rhs.
  ///
  /// @param[in] rhs
  ///     A const FileSpec object reference to assign to this object.
  ///
  /// @return
  ///     A const reference to this object.
  //------------------------------------------------------------------
  const FileSpec &operator=(const FileSpec &rhs);

  //------------------------------------------------------------------
  /// Equal to operator
  ///
  /// Tests if this object is equal to \a rhs.
  ///
  /// @param[in] rhs
  ///     A const FileSpec object reference to compare this object
  ///     to.
  ///
  /// @return
  ///     \b true if this object is equal to \a rhs, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  bool operator==(const FileSpec &rhs) const;

  //------------------------------------------------------------------
  /// Not equal to operator
  ///
  /// Tests if this object is not equal to \a rhs.
  ///
  /// @param[in] rhs
  ///     A const FileSpec object reference to compare this object
  ///     to.
  ///
  /// @return
  ///     \b true if this object is equal to \a rhs, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  bool operator!=(const FileSpec &rhs) const;

  //------------------------------------------------------------------
  /// Less than to operator
  ///
  /// Tests if this object is less than \a rhs.
  ///
  /// @param[in] rhs
  ///     A const FileSpec object reference to compare this object
  ///     to.
  ///
  /// @return
  ///     \b true if this object is less than \a rhs, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  bool operator<(const FileSpec &rhs) const;

  //------------------------------------------------------------------
  /// Convert to pointer operator.
  ///
  /// This allows code to check a FileSpec object to see if it contains
  /// anything valid using code such as:
  ///
  /// @code
  /// FileSpec file_spec(...);
  /// if (file_spec)
  /// { ...
  /// @endcode
  ///
  /// @return
  ///     A pointer to this object if either the directory or filename
  ///     is valid, nullptr otherwise.
  //------------------------------------------------------------------
  explicit operator bool() const;

  //------------------------------------------------------------------
  /// Logical NOT operator.
  ///
  /// This allows code to check a FileSpec object to see if it is invalid
  /// using code such as:
  ///
  /// @code
  /// FileSpec file_spec(...);
  /// if (!file_spec)
  /// { ...
  /// @endcode
  ///
  /// @return
  ///     Returns \b true if the object has an empty directory and
  ///     filename, \b false otherwise.
  //------------------------------------------------------------------
  bool operator!() const;

  //------------------------------------------------------------------
  /// Clears the object state.
  ///
  /// Clear this object by releasing both the directory and filename string
  /// values and reverting them to empty strings.
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  /// Compare two FileSpec objects.
  ///
  /// If \a full is true, then both the directory and the filename must match.
  /// If \a full is false, then the directory names for \a lhs and \a rhs are
  /// only compared if they are both not empty. This allows a FileSpec object
  /// to only contain a filename and it can match FileSpec objects that have
  /// matching filenames with different paths.
  ///
  /// @param[in] lhs
  ///     A const reference to the Left Hand Side object to compare.
  ///
  /// @param[in] rhs
  ///     A const reference to the Right Hand Side object to compare.
  ///
  /// @param[in] full
  ///     If true, then both the directory and filenames will have to
  ///     match for a compare to return zero (equal to). If false
  ///     and either directory from \a lhs or \a rhs is empty, then
  ///     only the filename will be compared, else a full comparison
  ///     is done.
  ///
  /// @return
  ///     @li -1 if \a lhs is less than \a rhs
  ///     @li 0 if \a lhs is equal to \a rhs
  ///     @li 1 if \a lhs is greater than \a rhs
  //------------------------------------------------------------------
  static int Compare(const FileSpec &lhs, const FileSpec &rhs, bool full);

  static bool Equal(const FileSpec &a, const FileSpec &b, bool full);

  //------------------------------------------------------------------
  /// Case sensitivity of path.
  ///
  /// @return
  ///     \b true if the file path is case sensitive (POSIX), false
  ///		if case insensitive (Windows).
  //------------------------------------------------------------------
  bool IsCaseSensitive() const { return m_style != Style::windows; }

  //------------------------------------------------------------------
  /// Dump this object to a Stream.
  ///
  /// Dump the object to the supplied stream \a s. If the object contains a
  /// valid directory name, it will be displayed followed by a directory
  /// delimiter, and the filename.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  //------------------------------------------------------------------
  void Dump(Stream *s) const;

  Style GetPathStyle() const;

  //------------------------------------------------------------------
  /// Directory string get accessor.
  ///
  /// @return
  ///     A reference to the directory string object.
  //------------------------------------------------------------------
  ConstString &GetDirectory();

  //------------------------------------------------------------------
  /// Directory string const get accessor.
  ///
  /// @return
  ///     A const reference to the directory string object.
  //------------------------------------------------------------------
  const ConstString &GetDirectory() const;

  //------------------------------------------------------------------
  /// Filename string get accessor.
  ///
  /// @return
  ///     A reference to the filename string object.
  //------------------------------------------------------------------
  ConstString &GetFilename();

  //------------------------------------------------------------------
  /// Filename string const get accessor.
  ///
  /// @return
  ///     A const reference to the filename string object.
  //------------------------------------------------------------------
  const ConstString &GetFilename() const;

  //------------------------------------------------------------------
  /// Returns true if the filespec represents an implementation source file
  /// (files with a ".c", ".cpp", ".m", ".mm" (many more) extension).
  ///
  /// @return
  ///     \b true if the filespec represents an implementation source
  ///     file, \b false otherwise.
  //------------------------------------------------------------------
  bool IsSourceImplementationFile() const;

  //------------------------------------------------------------------
  /// Returns true if the filespec represents a relative path.
  ///
  /// @return
  ///     \b true if the filespec represents a relative path,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  bool IsRelative() const;

  //------------------------------------------------------------------
  /// Returns true if the filespec represents an absolute path.
  ///
  /// @return
  ///     \b true if the filespec represents an absolute path,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  bool IsAbsolute() const;

  /// Temporary helper for FileSystem change.
  void SetPath(llvm::StringRef p) { SetFile(p); }

  //------------------------------------------------------------------
  /// Extract the full path to the file.
  ///
  /// Extract the directory and path into a fixed buffer. This is needed as
  /// the directory and path are stored in separate string values.
  ///
  /// @param[out] path
  ///     The buffer in which to place the extracted full path.
  ///
  /// @param[in] max_path_length
  ///     The maximum length of \a path.
  ///
  /// @return
  ///     Returns the number of characters that would be needed to
  ///     properly copy the full path into \a path. If the returned
  ///     number is less than \a max_path_length, then the path is
  ///     properly copied and terminated. If the return value is
  ///     >= \a max_path_length, then the path was truncated (but is
  ///     still NULL terminated).
  //------------------------------------------------------------------
  size_t GetPath(char *path, size_t max_path_length,
                 bool denormalize = true) const;

  //------------------------------------------------------------------
  /// Extract the full path to the file.
  ///
  /// Extract the directory and path into a std::string, which is returned.
  ///
  /// @return
  ///     Returns a std::string with the directory and filename
  ///     concatenated.
  //------------------------------------------------------------------
  std::string GetPath(bool denormalize = true) const;

  const char *GetCString(bool denormalize = true) const;

  //------------------------------------------------------------------
  /// Extract the full path to the file.
  ///
  /// Extract the directory and path into an llvm::SmallVectorImpl<>
  ///
  /// @return
  ///     Returns a std::string with the directory and filename
  ///     concatenated.
  //------------------------------------------------------------------
  void GetPath(llvm::SmallVectorImpl<char> &path,
               bool denormalize = true) const;

  //------------------------------------------------------------------
  /// Extract the extension of the file.
  ///
  /// Returns a ConstString that represents the extension of the filename for
  /// this FileSpec object. If this object does not represent a file, or the
  /// filename has no extension, ConstString(nullptr) is returned. The dot
  /// ('.') character is not returned as part of the extension
  ///
  /// @return
  ///     Returns the extension of the file as a ConstString object.
  //------------------------------------------------------------------
  ConstString GetFileNameExtension() const;

  //------------------------------------------------------------------
  /// Return the filename without the extension part
  ///
  /// Returns a ConstString that represents the filename of this object
  /// without the extension part (e.g. for a file named "foo.bar", "foo" is
  /// returned)
  ///
  /// @return
  ///     Returns the filename without extension
  ///     as a ConstString object.
  //------------------------------------------------------------------
  ConstString GetFileNameStrippingExtension() const;

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

  //------------------------------------------------------------------
  /// Change the file specified with a new path.
  ///
  /// Update the contents of this object with a new path. The path will be
  /// split up into a directory and filename and stored as uniqued string
  /// values for quick comparison and efficient memory usage.
  ///
  /// @param[in] path
  ///     A full, partial, or relative path to a file.
  ///
  /// @param[in] resolve_path
  ///     If \b true, then we will try to resolve links the path using
  ///     the static FileSpec::Resolve.
  //------------------------------------------------------------------
  void SetFile(llvm::StringRef path, Style style);

  void SetFile(llvm::StringRef path, const llvm::Triple &Triple);

  bool IsResolved() const { return m_is_resolved; }

  //------------------------------------------------------------------
  /// Set if the file path has been resolved or not.
  ///
  /// If you know a file path is already resolved and avoided passing a \b
  /// true parameter for any functions that take a "bool resolve_path"
  /// parameter, you can set the value manually using this call to make sure
  /// we don't try and resolve it later, or try and resolve a path that has
  /// already been resolved.
  ///
  /// @param[in] is_resolved
  ///     A boolean value that will replace the current value that
  ///     indicates if the paths in this object have been resolved.
  //------------------------------------------------------------------
  void SetIsResolved(bool is_resolved) { m_is_resolved = is_resolved; }

  FileSpec CopyByAppendingPathComponent(llvm::StringRef component) const;
  FileSpec CopyByRemovingLastPathComponent() const;

  void PrependPathComponent(llvm::StringRef component);
  void PrependPathComponent(const FileSpec &new_path);

  void AppendPathComponent(llvm::StringRef component);
  void AppendPathComponent(const FileSpec &new_path);

  //------------------------------------------------------------------
  /// Removes the last path component by replacing the current path with its
  /// parent. When the current path has no parent, this is a no-op.
  ///
  /// @return
  ///     A boolean value indicating whether the path was updated.
  //------------------------------------------------------------------
  bool RemoveLastPathComponent();

  ConstString GetLastPathComponent() const;

protected:
  //------------------------------------------------------------------
  // Convenience method for setting the file without changing the style.
  //------------------------------------------------------------------
  void SetFile(llvm::StringRef path);

  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  ConstString m_directory;            ///< The uniqued directory path
  ConstString m_filename;             ///< The uniqued filename path
  mutable bool m_is_resolved = false; ///< True if this path has been resolved.
  Style m_style; ///< The syntax that this path uses (e.g. Windows / Posix)
};

//----------------------------------------------------------------------
/// Dump a FileSpec object to a stream
//----------------------------------------------------------------------
Stream &operator<<(Stream &s, const FileSpec &f);

} // namespace lldb_private

namespace llvm {

/// Implementation of format_provider<T> for FileSpec.
///
/// The options string of a FileSpec has the grammar:
///
///   file_spec_options   :: (empty) | F | D
///
///   =======================================================
///   |  style  |     Meaning          |      Example       |
///   -------------------------------------------------------
///   |         |                      |  Input   |  Output |
///   =======================================================
///   |    F    | Only print filename  | /foo/bar |   bar   |
///   |    D    | Only print directory | /foo/bar |  /foo/  |
///   | (empty) | Print file and dir   |          |         |
///   =======================================================
///
/// Any other value is considered an invalid format string.
///
template <> struct format_provider<lldb_private::FileSpec> {
  static void format(const lldb_private::FileSpec &F, llvm::raw_ostream &Stream,
                     StringRef Style);
};
} // namespace llvm

#endif // liblldb_FileSpec_h_
