//===-- Path.cpp - Implement OS Path Concept ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the operating system Path API.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Path.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include <cctype>
#include <cstring>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

using namespace llvm;
using namespace llvm::support::endian;

namespace {
  using llvm::StringRef;
  using llvm::sys::path::is_separator;
  using llvm::sys::path::Style;

  inline Style real_style(Style style) {
#ifdef _WIN32
    return (style == Style::posix) ? Style::posix : Style::windows;
#else
    return (style == Style::windows) ? Style::windows : Style::posix;
#endif
  }

  inline const char *separators(Style style) {
    if (real_style(style) == Style::windows)
      return "\\/";
    return "/";
  }

  inline char preferred_separator(Style style) {
    if (real_style(style) == Style::windows)
      return '\\';
    return '/';
  }

  StringRef find_first_component(StringRef path, Style style) {
    // Look for this first component in the following order.
    // * empty (in this case we return an empty string)
    // * either C: or {//,\\}net.
    // * {/,\}
    // * {file,directory}name

    if (path.empty())
      return path;

    if (real_style(style) == Style::windows) {
      // C:
      if (path.size() >= 2 &&
          std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
        return path.substr(0, 2);
    }

    // //net
    if ((path.size() > 2) && is_separator(path[0], style) &&
        path[0] == path[1] && !is_separator(path[2], style)) {
      // Find the next directory separator.
      size_t end = path.find_first_of(separators(style), 2);
      return path.substr(0, end);
    }

    // {/,\}
    if (is_separator(path[0], style))
      return path.substr(0, 1);

    // * {file,directory}name
    size_t end = path.find_first_of(separators(style));
    return path.substr(0, end);
  }

  // Returns the first character of the filename in str. For paths ending in
  // '/', it returns the position of the '/'.
  size_t filename_pos(StringRef str, Style style) {
    if (str.size() > 0 && is_separator(str[str.size() - 1], style))
      return str.size() - 1;

    size_t pos = str.find_last_of(separators(style), str.size() - 1);

    if (real_style(style) == Style::windows) {
      if (pos == StringRef::npos)
        pos = str.find_last_of(':', str.size() - 2);
    }

    if (pos == StringRef::npos || (pos == 1 && is_separator(str[0], style)))
      return 0;

    return pos + 1;
  }

  // Returns the position of the root directory in str. If there is no root
  // directory in str, it returns StringRef::npos.
  size_t root_dir_start(StringRef str, Style style) {
    // case "c:/"
    if (real_style(style) == Style::windows) {
      if (str.size() > 2 && str[1] == ':' && is_separator(str[2], style))
        return 2;
    }

    // case "//net"
    if (str.size() > 3 && is_separator(str[0], style) && str[0] == str[1] &&
        !is_separator(str[2], style)) {
      return str.find_first_of(separators(style), 2);
    }

    // case "/"
    if (str.size() > 0 && is_separator(str[0], style))
      return 0;

    return StringRef::npos;
  }

  // Returns the position past the end of the "parent path" of path. The parent
  // path will not end in '/', unless the parent is the root directory. If the
  // path has no parent, 0 is returned.
  size_t parent_path_end(StringRef path, Style style) {
    size_t end_pos = filename_pos(path, style);

    bool filename_was_sep =
        path.size() > 0 && is_separator(path[end_pos], style);

    // Skip separators until we reach root dir (or the start of the string).
    size_t root_dir_pos = root_dir_start(path, style);
    while (end_pos > 0 &&
           (root_dir_pos == StringRef::npos || end_pos > root_dir_pos) &&
           is_separator(path[end_pos - 1], style))
      --end_pos;

    if (end_pos == root_dir_pos && !filename_was_sep) {
      // We've reached the root dir and the input path was *not* ending in a
      // sequence of slashes. Include the root dir in the parent path.
      return root_dir_pos + 1;
    }

    // Otherwise, just include before the last slash.
    return end_pos;
  }
} // end unnamed namespace

enum FSEntity {
  FS_Dir,
  FS_File,
  FS_Name
};

static std::error_code
createUniqueEntity(const Twine &Model, int &ResultFD,
                   SmallVectorImpl<char> &ResultPath, bool MakeAbsolute,
                   unsigned Mode, FSEntity Type,
                   sys::fs::OpenFlags Flags = sys::fs::OF_None) {
  SmallString<128> ModelStorage;
  Model.toVector(ModelStorage);

  if (MakeAbsolute) {
    // Make model absolute by prepending a temp directory if it's not already.
    if (!sys::path::is_absolute(Twine(ModelStorage))) {
      SmallString<128> TDir;
      sys::path::system_temp_directory(true, TDir);
      sys::path::append(TDir, Twine(ModelStorage));
      ModelStorage.swap(TDir);
    }
  }

  // From here on, DO NOT modify model. It may be needed if the randomly chosen
  // path already exists.
  ResultPath = ModelStorage;
  // Null terminate.
  ResultPath.push_back(0);
  ResultPath.pop_back();

  // Limit the number of attempts we make, so that we don't infinite loop. E.g.
  // "permission denied" could be for a specific file (so we retry with a
  // different name) or for the whole directory (retry would always fail).
  // Checking which is racy, so we try a number of times, then give up.
  std::error_code EC;
  for (int Retries = 128; Retries > 0; --Retries) {
    // Replace '%' with random chars.
    for (unsigned i = 0, e = ModelStorage.size(); i != e; ++i) {
      if (ModelStorage[i] == '%')
        ResultPath[i] =
            "0123456789abcdef"[sys::Process::GetRandomNumber() & 15];
    }

    // Try to open + create the file.
    switch (Type) {
    case FS_File: {
      EC = sys::fs::openFileForReadWrite(Twine(ResultPath.begin()), ResultFD,
                                         sys::fs::CD_CreateNew, Flags, Mode);
      if (EC) {
        // errc::permission_denied happens on Windows when we try to open a file
        // that has been marked for deletion.
        if (EC == errc::file_exists || EC == errc::permission_denied)
          continue;
        return EC;
      }

      return std::error_code();
    }

    case FS_Name: {
      EC = sys::fs::access(ResultPath.begin(), sys::fs::AccessMode::Exist);
      if (EC == errc::no_such_file_or_directory)
        return std::error_code();
      if (EC)
        return EC;
      continue;
    }

    case FS_Dir: {
      EC = sys::fs::create_directory(ResultPath.begin(), false);
      if (EC) {
        if (EC == errc::file_exists)
          continue;
        return EC;
      }
      return std::error_code();
    }
    }
    llvm_unreachable("Invalid Type");
  }
  return EC;
}

namespace llvm {
namespace sys  {
namespace path {

const_iterator begin(StringRef path, Style style) {
  const_iterator i;
  i.Path      = path;
  i.Component = find_first_component(path, style);
  i.Position  = 0;
  i.S = style;
  return i;
}

const_iterator end(StringRef path) {
  const_iterator i;
  i.Path      = path;
  i.Position  = path.size();
  return i;
}

const_iterator &const_iterator::operator++() {
  assert(Position < Path.size() && "Tried to increment past end!");

  // Increment Position to past the current component
  Position += Component.size();

  // Check for end.
  if (Position == Path.size()) {
    Component = StringRef();
    return *this;
  }

  // Both POSIX and Windows treat paths that begin with exactly two separators
  // specially.
  bool was_net = Component.size() > 2 && is_separator(Component[0], S) &&
                 Component[1] == Component[0] && !is_separator(Component[2], S);

  // Handle separators.
  if (is_separator(Path[Position], S)) {
    // Root dir.
    if (was_net ||
        // c:/
        (real_style(S) == Style::windows && Component.endswith(":"))) {
      Component = Path.substr(Position, 1);
      return *this;
    }

    // Skip extra separators.
    while (Position != Path.size() && is_separator(Path[Position], S)) {
      ++Position;
    }

    // Treat trailing '/' as a '.', unless it is the root dir.
    if (Position == Path.size() && Component != "/") {
      --Position;
      Component = ".";
      return *this;
    }
  }

  // Find next component.
  size_t end_pos = Path.find_first_of(separators(S), Position);
  Component = Path.slice(Position, end_pos);

  return *this;
}

bool const_iterator::operator==(const const_iterator &RHS) const {
  return Path.begin() == RHS.Path.begin() && Position == RHS.Position;
}

ptrdiff_t const_iterator::operator-(const const_iterator &RHS) const {
  return Position - RHS.Position;
}

reverse_iterator rbegin(StringRef Path, Style style) {
  reverse_iterator I;
  I.Path = Path;
  I.Position = Path.size();
  I.S = style;
  return ++I;
}

reverse_iterator rend(StringRef Path) {
  reverse_iterator I;
  I.Path = Path;
  I.Component = Path.substr(0, 0);
  I.Position = 0;
  return I;
}

reverse_iterator &reverse_iterator::operator++() {
  size_t root_dir_pos = root_dir_start(Path, S);

  // Skip separators unless it's the root directory.
  size_t end_pos = Position;
  while (end_pos > 0 && (end_pos - 1) != root_dir_pos &&
         is_separator(Path[end_pos - 1], S))
    --end_pos;

  // Treat trailing '/' as a '.', unless it is the root dir.
  if (Position == Path.size() && !Path.empty() &&
      is_separator(Path.back(), S) &&
      (root_dir_pos == StringRef::npos || end_pos - 1 > root_dir_pos)) {
    --Position;
    Component = ".";
    return *this;
  }

  // Find next separator.
  size_t start_pos = filename_pos(Path.substr(0, end_pos), S);
  Component = Path.slice(start_pos, end_pos);
  Position = start_pos;
  return *this;
}

bool reverse_iterator::operator==(const reverse_iterator &RHS) const {
  return Path.begin() == RHS.Path.begin() && Component == RHS.Component &&
         Position == RHS.Position;
}

ptrdiff_t reverse_iterator::operator-(const reverse_iterator &RHS) const {
  return Position - RHS.Position;
}

StringRef root_path(StringRef path, Style style) {
  const_iterator b = begin(path, style), pos = b, e = end(path);
  if (b != e) {
    bool has_net =
        b->size() > 2 && is_separator((*b)[0], style) && (*b)[1] == (*b)[0];
    bool has_drive = (real_style(style) == Style::windows) && b->endswith(":");

    if (has_net || has_drive) {
      if ((++pos != e) && is_separator((*pos)[0], style)) {
        // {C:/,//net/}, so get the first two components.
        return path.substr(0, b->size() + pos->size());
      } else {
        // just {C:,//net}, return the first component.
        return *b;
      }
    }

    // POSIX style root directory.
    if (is_separator((*b)[0], style)) {
      return *b;
    }
  }

  return StringRef();
}

StringRef root_name(StringRef path, Style style) {
  const_iterator b = begin(path, style), e = end(path);
  if (b != e) {
    bool has_net =
        b->size() > 2 && is_separator((*b)[0], style) && (*b)[1] == (*b)[0];
    bool has_drive = (real_style(style) == Style::windows) && b->endswith(":");

    if (has_net || has_drive) {
      // just {C:,//net}, return the first component.
      return *b;
    }
  }

  // No path or no name.
  return StringRef();
}

StringRef root_directory(StringRef path, Style style) {
  const_iterator b = begin(path, style), pos = b, e = end(path);
  if (b != e) {
    bool has_net =
        b->size() > 2 && is_separator((*b)[0], style) && (*b)[1] == (*b)[0];
    bool has_drive = (real_style(style) == Style::windows) && b->endswith(":");

    if ((has_net || has_drive) &&
        // {C:,//net}, skip to the next component.
        (++pos != e) && is_separator((*pos)[0], style)) {
      return *pos;
    }

    // POSIX style root directory.
    if (!has_net && is_separator((*b)[0], style)) {
      return *b;
    }
  }

  // No path or no root.
  return StringRef();
}

StringRef relative_path(StringRef path, Style style) {
  StringRef root = root_path(path, style);
  return path.substr(root.size());
}

void append(SmallVectorImpl<char> &path, Style style, const Twine &a,
            const Twine &b, const Twine &c, const Twine &d) {
  SmallString<32> a_storage;
  SmallString<32> b_storage;
  SmallString<32> c_storage;
  SmallString<32> d_storage;

  SmallVector<StringRef, 4> components;
  if (!a.isTriviallyEmpty()) components.push_back(a.toStringRef(a_storage));
  if (!b.isTriviallyEmpty()) components.push_back(b.toStringRef(b_storage));
  if (!c.isTriviallyEmpty()) components.push_back(c.toStringRef(c_storage));
  if (!d.isTriviallyEmpty()) components.push_back(d.toStringRef(d_storage));

  for (auto &component : components) {
    bool path_has_sep =
        !path.empty() && is_separator(path[path.size() - 1], style);
    if (path_has_sep) {
      // Strip separators from beginning of component.
      size_t loc = component.find_first_not_of(separators(style));
      StringRef c = component.substr(loc);

      // Append it.
      path.append(c.begin(), c.end());
      continue;
    }

    bool component_has_sep =
        !component.empty() && is_separator(component[0], style);
    if (!component_has_sep &&
        !(path.empty() || has_root_name(component, style))) {
      // Add a separator.
      path.push_back(preferred_separator(style));
    }

    path.append(component.begin(), component.end());
  }
}

void append(SmallVectorImpl<char> &path, const Twine &a, const Twine &b,
            const Twine &c, const Twine &d) {
  append(path, Style::native, a, b, c, d);
}

void append(SmallVectorImpl<char> &path, const_iterator begin,
            const_iterator end, Style style) {
  for (; begin != end; ++begin)
    path::append(path, style, *begin);
}

StringRef parent_path(StringRef path, Style style) {
  size_t end_pos = parent_path_end(path, style);
  if (end_pos == StringRef::npos)
    return StringRef();
  else
    return path.substr(0, end_pos);
}

void remove_filename(SmallVectorImpl<char> &path, Style style) {
  size_t end_pos = parent_path_end(StringRef(path.begin(), path.size()), style);
  if (end_pos != StringRef::npos)
    path.set_size(end_pos);
}

void replace_extension(SmallVectorImpl<char> &path, const Twine &extension,
                       Style style) {
  StringRef p(path.begin(), path.size());
  SmallString<32> ext_storage;
  StringRef ext = extension.toStringRef(ext_storage);

  // Erase existing extension.
  size_t pos = p.find_last_of('.');
  if (pos != StringRef::npos && pos >= filename_pos(p, style))
    path.set_size(pos);

  // Append '.' if needed.
  if (ext.size() > 0 && ext[0] != '.')
    path.push_back('.');

  // Append extension.
  path.append(ext.begin(), ext.end());
}

void replace_path_prefix(SmallVectorImpl<char> &Path,
                         const StringRef &OldPrefix, const StringRef &NewPrefix,
                         Style style) {
  if (OldPrefix.empty() && NewPrefix.empty())
    return;

  StringRef OrigPath(Path.begin(), Path.size());
  if (!OrigPath.startswith(OldPrefix))
    return;

  // If prefixes have the same size we can simply copy the new one over.
  if (OldPrefix.size() == NewPrefix.size()) {
    llvm::copy(NewPrefix, Path.begin());
    return;
  }

  StringRef RelPath = OrigPath.substr(OldPrefix.size());
  SmallString<256> NewPath;
  path::append(NewPath, style, NewPrefix);
  path::append(NewPath, style, RelPath);
  Path.swap(NewPath);
}

void native(const Twine &path, SmallVectorImpl<char> &result, Style style) {
  assert((!path.isSingleStringRef() ||
          path.getSingleStringRef().data() != result.data()) &&
         "path and result are not allowed to overlap!");
  // Clear result.
  result.clear();
  path.toVector(result);
  native(result, style);
}

void native(SmallVectorImpl<char> &Path, Style style) {
  if (Path.empty())
    return;
  if (real_style(style) == Style::windows) {
    std::replace(Path.begin(), Path.end(), '/', '\\');
    if (Path[0] == '~' && (Path.size() == 1 || is_separator(Path[1], style))) {
      SmallString<128> PathHome;
      home_directory(PathHome);
      PathHome.append(Path.begin() + 1, Path.end());
      Path = PathHome;
    }
  } else {
    for (auto PI = Path.begin(), PE = Path.end(); PI < PE; ++PI) {
      if (*PI == '\\') {
        auto PN = PI + 1;
        if (PN < PE && *PN == '\\')
          ++PI; // increment once, the for loop will move over the escaped slash
        else
          *PI = '/';
      }
    }
  }
}

std::string convert_to_slash(StringRef path, Style style) {
  if (real_style(style) != Style::windows)
    return path;

  std::string s = path.str();
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}

StringRef filename(StringRef path, Style style) { return *rbegin(path, style); }

StringRef stem(StringRef path, Style style) {
  StringRef fname = filename(path, style);
  size_t pos = fname.find_last_of('.');
  if (pos == StringRef::npos)
    return fname;
  else
    if ((fname.size() == 1 && fname == ".") ||
        (fname.size() == 2 && fname == ".."))
      return fname;
    else
      return fname.substr(0, pos);
}

StringRef extension(StringRef path, Style style) {
  StringRef fname = filename(path, style);
  size_t pos = fname.find_last_of('.');
  if (pos == StringRef::npos)
    return StringRef();
  else
    if ((fname.size() == 1 && fname == ".") ||
        (fname.size() == 2 && fname == ".."))
      return StringRef();
    else
      return fname.substr(pos);
}

bool is_separator(char value, Style style) {
  if (value == '/')
    return true;
  if (real_style(style) == Style::windows)
    return value == '\\';
  return false;
}

StringRef get_separator(Style style) {
  if (real_style(style) == Style::windows)
    return "\\";
  return "/";
}

bool has_root_name(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !root_name(p, style).empty();
}

bool has_root_directory(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !root_directory(p, style).empty();
}

bool has_root_path(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !root_path(p, style).empty();
}

bool has_relative_path(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !relative_path(p, style).empty();
}

bool has_filename(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !filename(p, style).empty();
}

bool has_parent_path(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !parent_path(p, style).empty();
}

bool has_stem(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !stem(p, style).empty();
}

bool has_extension(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  return !extension(p, style).empty();
}

bool is_absolute(const Twine &path, Style style) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  bool rootDir = has_root_directory(p, style);
  bool rootName =
      (real_style(style) != Style::windows) || has_root_name(p, style);

  return rootDir && rootName;
}

bool is_relative(const Twine &path, Style style) {
  return !is_absolute(path, style);
}

StringRef remove_leading_dotslash(StringRef Path, Style style) {
  // Remove leading "./" (or ".//" or "././" etc.)
  while (Path.size() > 2 && Path[0] == '.' && is_separator(Path[1], style)) {
    Path = Path.substr(2);
    while (Path.size() > 0 && is_separator(Path[0], style))
      Path = Path.substr(1);
  }
  return Path;
}

static SmallString<256> remove_dots(StringRef path, bool remove_dot_dot,
                                    Style style) {
  SmallVector<StringRef, 16> components;

  // Skip the root path, then look for traversal in the components.
  StringRef rel = path::relative_path(path, style);
  for (StringRef C :
       llvm::make_range(path::begin(rel, style), path::end(rel))) {
    if (C == ".")
      continue;
    // Leading ".." will remain in the path unless it's at the root.
    if (remove_dot_dot && C == "..") {
      if (!components.empty() && components.back() != "..") {
        components.pop_back();
        continue;
      }
      if (path::is_absolute(path, style))
        continue;
    }
    components.push_back(C);
  }

  SmallString<256> buffer = path::root_path(path, style);
  for (StringRef C : components)
    path::append(buffer, style, C);
  return buffer;
}

bool remove_dots(SmallVectorImpl<char> &path, bool remove_dot_dot,
                 Style style) {
  StringRef p(path.data(), path.size());

  SmallString<256> result = remove_dots(p, remove_dot_dot, style);
  if (result == path)
    return false;

  path.swap(result);
  return true;
}

} // end namespace path

namespace fs {

std::error_code getUniqueID(const Twine Path, UniqueID &Result) {
  file_status Status;
  std::error_code EC = status(Path, Status);
  if (EC)
    return EC;
  Result = Status.getUniqueID();
  return std::error_code();
}

std::error_code createUniqueFile(const Twine &Model, int &ResultFd,
                                 SmallVectorImpl<char> &ResultPath,
                                 unsigned Mode) {
  return createUniqueEntity(Model, ResultFd, ResultPath, false, Mode, FS_File);
}

static std::error_code createUniqueFile(const Twine &Model, int &ResultFd,
                                        SmallVectorImpl<char> &ResultPath,
                                        unsigned Mode, OpenFlags Flags) {
  return createUniqueEntity(Model, ResultFd, ResultPath, false, Mode, FS_File,
                            Flags);
}

std::error_code createUniqueFile(const Twine &Model,
                                 SmallVectorImpl<char> &ResultPath,
                                 unsigned Mode) {
  int FD;
  auto EC = createUniqueFile(Model, FD, ResultPath, Mode);
  if (EC)
    return EC;
  // FD is only needed to avoid race conditions. Close it right away.
  close(FD);
  return EC;
}

static std::error_code
createTemporaryFile(const Twine &Model, int &ResultFD,
                    llvm::SmallVectorImpl<char> &ResultPath, FSEntity Type) {
  SmallString<128> Storage;
  StringRef P = Model.toNullTerminatedStringRef(Storage);
  assert(P.find_first_of(separators(Style::native)) == StringRef::npos &&
         "Model must be a simple filename.");
  // Use P.begin() so that createUniqueEntity doesn't need to recreate Storage.
  return createUniqueEntity(P.begin(), ResultFD, ResultPath, true,
                            owner_read | owner_write, Type);
}

static std::error_code
createTemporaryFile(const Twine &Prefix, StringRef Suffix, int &ResultFD,
                    llvm::SmallVectorImpl<char> &ResultPath, FSEntity Type) {
  const char *Middle = Suffix.empty() ? "-%%%%%%" : "-%%%%%%.";
  return createTemporaryFile(Prefix + Middle + Suffix, ResultFD, ResultPath,
                             Type);
}

std::error_code createTemporaryFile(const Twine &Prefix, StringRef Suffix,
                                    int &ResultFD,
                                    SmallVectorImpl<char> &ResultPath) {
  return createTemporaryFile(Prefix, Suffix, ResultFD, ResultPath, FS_File);
}

std::error_code createTemporaryFile(const Twine &Prefix, StringRef Suffix,
                                    SmallVectorImpl<char> &ResultPath) {
  int FD;
  auto EC = createTemporaryFile(Prefix, Suffix, FD, ResultPath);
  if (EC)
    return EC;
  // FD is only needed to avoid race conditions. Close it right away.
  close(FD);
  return EC;
}


// This is a mkdtemp with a different pattern. We use createUniqueEntity mostly
// for consistency. We should try using mkdtemp.
std::error_code createUniqueDirectory(const Twine &Prefix,
                                      SmallVectorImpl<char> &ResultPath) {
  int Dummy;
  return createUniqueEntity(Prefix + "-%%%%%%", Dummy, ResultPath, true, 0,
                            FS_Dir);
}

std::error_code
getPotentiallyUniqueFileName(const Twine &Model,
                             SmallVectorImpl<char> &ResultPath) {
  int Dummy;
  return createUniqueEntity(Model, Dummy, ResultPath, false, 0, FS_Name);
}

std::error_code
getPotentiallyUniqueTempFileName(const Twine &Prefix, StringRef Suffix,
                                 SmallVectorImpl<char> &ResultPath) {
  int Dummy;
  return createTemporaryFile(Prefix, Suffix, Dummy, ResultPath, FS_Name);
}

void make_absolute(const Twine &current_directory,
                   SmallVectorImpl<char> &path) {
  StringRef p(path.data(), path.size());

  bool rootDirectory = path::has_root_directory(p);
  bool rootName =
      (real_style(Style::native) != Style::windows) || path::has_root_name(p);

  // Already absolute.
  if (rootName && rootDirectory)
    return;

  // All of the following conditions will need the current directory.
  SmallString<128> current_dir;
  current_directory.toVector(current_dir);

  // Relative path. Prepend the current directory.
  if (!rootName && !rootDirectory) {
    // Append path to the current directory.
    path::append(current_dir, p);
    // Set path to the result.
    path.swap(current_dir);
    return;
  }

  if (!rootName && rootDirectory) {
    StringRef cdrn = path::root_name(current_dir);
    SmallString<128> curDirRootName(cdrn.begin(), cdrn.end());
    path::append(curDirRootName, p);
    // Set path to the result.
    path.swap(curDirRootName);
    return;
  }

  if (rootName && !rootDirectory) {
    StringRef pRootName      = path::root_name(p);
    StringRef bRootDirectory = path::root_directory(current_dir);
    StringRef bRelativePath  = path::relative_path(current_dir);
    StringRef pRelativePath  = path::relative_path(p);

    SmallString<128> res;
    path::append(res, pRootName, bRootDirectory, bRelativePath, pRelativePath);
    path.swap(res);
    return;
  }

  llvm_unreachable("All rootName and rootDirectory combinations should have "
                   "occurred above!");
}

std::error_code make_absolute(SmallVectorImpl<char> &path) {
  if (path::is_absolute(path))
    return {};

  SmallString<128> current_dir;
  if (std::error_code ec = current_path(current_dir))
    return ec;

  make_absolute(current_dir, path);
  return {};
}

std::error_code create_directories(const Twine &Path, bool IgnoreExisting,
                                   perms Perms) {
  SmallString<128> PathStorage;
  StringRef P = Path.toStringRef(PathStorage);

  // Be optimistic and try to create the directory
  std::error_code EC = create_directory(P, IgnoreExisting, Perms);
  // If we succeeded, or had any error other than the parent not existing, just
  // return it.
  if (EC != errc::no_such_file_or_directory)
    return EC;

  // We failed because of a no_such_file_or_directory, try to create the
  // parent.
  StringRef Parent = path::parent_path(P);
  if (Parent.empty())
    return EC;

  if ((EC = create_directories(Parent, IgnoreExisting, Perms)))
      return EC;

  return create_directory(P, IgnoreExisting, Perms);
}

static std::error_code copy_file_internal(int ReadFD, int WriteFD) {
  const size_t BufSize = 4096;
  char *Buf = new char[BufSize];
  int BytesRead = 0, BytesWritten = 0;
  for (;;) {
    BytesRead = read(ReadFD, Buf, BufSize);
    if (BytesRead <= 0)
      break;
    while (BytesRead) {
      BytesWritten = write(WriteFD, Buf, BytesRead);
      if (BytesWritten < 0)
        break;
      BytesRead -= BytesWritten;
    }
    if (BytesWritten < 0)
      break;
  }
  delete[] Buf;

  if (BytesRead < 0 || BytesWritten < 0)
    return std::error_code(errno, std::generic_category());
  return std::error_code();
}

std::error_code copy_file(const Twine &From, const Twine &To) {
  int ReadFD, WriteFD;
  if (std::error_code EC = openFileForRead(From, ReadFD, OF_None))
    return EC;
  if (std::error_code EC =
          openFileForWrite(To, WriteFD, CD_CreateAlways, OF_None)) {
    close(ReadFD);
    return EC;
  }

  std::error_code EC = copy_file_internal(ReadFD, WriteFD);

  close(ReadFD);
  close(WriteFD);

  return EC;
}

std::error_code copy_file(const Twine &From, int ToFD) {
  int ReadFD;
  if (std::error_code EC = openFileForRead(From, ReadFD, OF_None))
    return EC;

  std::error_code EC = copy_file_internal(ReadFD, ToFD);

  close(ReadFD);

  return EC;
}

ErrorOr<MD5::MD5Result> md5_contents(int FD) {
  MD5 Hash;

  constexpr size_t BufSize = 4096;
  std::vector<uint8_t> Buf(BufSize);
  int BytesRead = 0;
  for (;;) {
    BytesRead = read(FD, Buf.data(), BufSize);
    if (BytesRead <= 0)
      break;
    Hash.update(makeArrayRef(Buf.data(), BytesRead));
  }

  if (BytesRead < 0)
    return std::error_code(errno, std::generic_category());
  MD5::MD5Result Result;
  Hash.final(Result);
  return Result;
}

ErrorOr<MD5::MD5Result> md5_contents(const Twine &Path) {
  int FD;
  if (auto EC = openFileForRead(Path, FD, OF_None))
    return EC;

  auto Result = md5_contents(FD);
  close(FD);
  return Result;
}

bool exists(const basic_file_status &status) {
  return status_known(status) && status.type() != file_type::file_not_found;
}

bool status_known(const basic_file_status &s) {
  return s.type() != file_type::status_error;
}

file_type get_file_type(const Twine &Path, bool Follow) {
  file_status st;
  if (status(Path, st, Follow))
    return file_type::status_error;
  return st.type();
}

bool is_directory(const basic_file_status &status) {
  return status.type() == file_type::directory_file;
}

std::error_code is_directory(const Twine &path, bool &result) {
  file_status st;
  if (std::error_code ec = status(path, st))
    return ec;
  result = is_directory(st);
  return std::error_code();
}

bool is_regular_file(const basic_file_status &status) {
  return status.type() == file_type::regular_file;
}

std::error_code is_regular_file(const Twine &path, bool &result) {
  file_status st;
  if (std::error_code ec = status(path, st))
    return ec;
  result = is_regular_file(st);
  return std::error_code();
}

bool is_symlink_file(const basic_file_status &status) {
  return status.type() == file_type::symlink_file;
}

std::error_code is_symlink_file(const Twine &path, bool &result) {
  file_status st;
  if (std::error_code ec = status(path, st, false))
    return ec;
  result = is_symlink_file(st);
  return std::error_code();
}

bool is_other(const basic_file_status &status) {
  return exists(status) &&
         !is_regular_file(status) &&
         !is_directory(status);
}

std::error_code is_other(const Twine &Path, bool &Result) {
  file_status FileStatus;
  if (std::error_code EC = status(Path, FileStatus))
    return EC;
  Result = is_other(FileStatus);
  return std::error_code();
}

void directory_entry::replace_filename(const Twine &Filename, file_type Type,
                                       basic_file_status Status) {
  SmallString<128> PathStr = path::parent_path(Path);
  path::append(PathStr, Filename);
  this->Path = PathStr.str();
  this->Type = Type;
  this->Status = Status;
}

ErrorOr<perms> getPermissions(const Twine &Path) {
  file_status Status;
  if (std::error_code EC = status(Path, Status))
    return EC;

  return Status.permissions();
}

} // end namespace fs
} // end namespace sys
} // end namespace llvm

// Include the truly platform-specific parts.
#if defined(LLVM_ON_UNIX)
#include "Unix/Path.inc"
#endif
#if defined(_WIN32)
#include "Windows/Path.inc"
#endif

namespace llvm {
namespace sys {
namespace fs {
TempFile::TempFile(StringRef Name, int FD) : TmpName(Name), FD(FD) {}
TempFile::TempFile(TempFile &&Other) { *this = std::move(Other); }
TempFile &TempFile::operator=(TempFile &&Other) {
  TmpName = std::move(Other.TmpName);
  FD = Other.FD;
  Other.Done = true;
  return *this;
}

TempFile::~TempFile() { assert(Done); }

Error TempFile::discard() {
  Done = true;
  std::error_code RemoveEC;
// On windows closing will remove the file.
#ifndef _WIN32
  // Always try to close and remove.
  if (!TmpName.empty()) {
    RemoveEC = fs::remove(TmpName);
    sys::DontRemoveFileOnSignal(TmpName);
  }
#endif

  if (!RemoveEC)
    TmpName = "";

  if (FD != -1 && close(FD) == -1) {
    std::error_code EC = std::error_code(errno, std::generic_category());
    return errorCodeToError(EC);
  }
  FD = -1;

  return errorCodeToError(RemoveEC);
}

Error TempFile::keep(const Twine &Name) {
  assert(!Done);
  Done = true;
  // Always try to close and rename.
#ifdef _WIN32
  // If we can't cancel the delete don't rename.
  auto H = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  std::error_code RenameEC = setDeleteDisposition(H, false);
  if (!RenameEC) {
    RenameEC = rename_fd(FD, Name);
    // If rename failed because it's cross-device, copy instead
    if (RenameEC ==
      std::error_code(ERROR_NOT_SAME_DEVICE, std::system_category())) {
      RenameEC = copy_file(TmpName, Name);
      setDeleteDisposition(H, true);
    }
  }

  // If we can't rename, discard the temporary file.
  if (RenameEC)
    setDeleteDisposition(H, true);
#else
  std::error_code RenameEC = fs::rename(TmpName, Name);
  if (RenameEC) {
    // If we can't rename, try to copy to work around cross-device link issues.
    RenameEC = sys::fs::copy_file(TmpName, Name);
    // If we can't rename or copy, discard the temporary file.
    if (RenameEC)
      remove(TmpName);
  }
  sys::DontRemoveFileOnSignal(TmpName);
#endif

  if (!RenameEC)
    TmpName = "";

  if (close(FD) == -1) {
    std::error_code EC(errno, std::generic_category());
    return errorCodeToError(EC);
  }
  FD = -1;

  return errorCodeToError(RenameEC);
}

Error TempFile::keep() {
  assert(!Done);
  Done = true;

#ifdef _WIN32
  auto H = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  if (std::error_code EC = setDeleteDisposition(H, false))
    return errorCodeToError(EC);
#else
  sys::DontRemoveFileOnSignal(TmpName);
#endif

  TmpName = "";

  if (close(FD) == -1) {
    std::error_code EC(errno, std::generic_category());
    return errorCodeToError(EC);
  }
  FD = -1;

  return Error::success();
}

Expected<TempFile> TempFile::create(const Twine &Model, unsigned Mode) {
  int FD;
  SmallString<128> ResultPath;
  if (std::error_code EC =
          createUniqueFile(Model, FD, ResultPath, Mode, OF_Delete))
    return errorCodeToError(EC);

  TempFile Ret(ResultPath, FD);
#ifndef _WIN32
  if (sys::RemoveFileOnSignal(ResultPath)) {
    // Make sure we delete the file when RemoveFileOnSignal fails.
    consumeError(Ret.discard());
    std::error_code EC(errc::operation_not_permitted);
    return errorCodeToError(EC);
  }
#endif
  return std::move(Ret);
}
}

} // end namsspace sys
} // end namespace llvm
