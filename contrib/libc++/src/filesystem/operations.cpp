//===--------------------- filesystem/ops.cpp -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "filesystem"
#include "array"
#include "iterator"
#include "fstream"
#include "random" /* for unique_path */
#include "string_view"
#include "type_traits"
#include "vector"
#include "cstdlib"
#include "climits"

#include "filesystem_common.h"

#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <fcntl.h> /* values for fchmodat */

#if defined(__linux__)
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
#include <sys/sendfile.h>
#define _LIBCPP_USE_SENDFILE
#endif
#elif defined(__APPLE__) || __has_include(<copyfile.h>)
#include <copyfile.h>
#define _LIBCPP_USE_COPYFILE
#endif

#if !defined(__APPLE__)
#define _LIBCPP_USE_CLOCK_GETTIME
#endif

#if !defined(CLOCK_REALTIME) || !defined(_LIBCPP_USE_CLOCK_GETTIME)
#include <sys/time.h> // for gettimeofday and timeval
#endif                // !defined(CLOCK_REALTIME)

#if defined(_LIBCPP_COMPILER_GCC)
#if _GNUC_VER < 500
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#endif

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

namespace {
namespace parser {

using string_view_t = path::__string_view;
using string_view_pair = pair<string_view_t, string_view_t>;
using PosPtr = path::value_type const*;

struct PathParser {
  enum ParserState : unsigned char {
    // Zero is a special sentinel value used by default constructed iterators.
    PS_BeforeBegin = path::iterator::_BeforeBegin,
    PS_InRootName = path::iterator::_InRootName,
    PS_InRootDir = path::iterator::_InRootDir,
    PS_InFilenames = path::iterator::_InFilenames,
    PS_InTrailingSep = path::iterator::_InTrailingSep,
    PS_AtEnd = path::iterator::_AtEnd
  };

  const string_view_t Path;
  string_view_t RawEntry;
  ParserState State;

private:
  PathParser(string_view_t P, ParserState State) noexcept : Path(P),
                                                            State(State) {}

public:
  PathParser(string_view_t P, string_view_t E, unsigned char S)
      : Path(P), RawEntry(E), State(static_cast<ParserState>(S)) {
    // S cannot be '0' or PS_BeforeBegin.
  }

  static PathParser CreateBegin(string_view_t P) noexcept {
    PathParser PP(P, PS_BeforeBegin);
    PP.increment();
    return PP;
  }

  static PathParser CreateEnd(string_view_t P) noexcept {
    PathParser PP(P, PS_AtEnd);
    return PP;
  }

  PosPtr peek() const noexcept {
    auto TkEnd = getNextTokenStartPos();
    auto End = getAfterBack();
    return TkEnd == End ? nullptr : TkEnd;
  }

  void increment() noexcept {
    const PosPtr End = getAfterBack();
    const PosPtr Start = getNextTokenStartPos();
    if (Start == End)
      return makeState(PS_AtEnd);

    switch (State) {
    case PS_BeforeBegin: {
      PosPtr TkEnd = consumeSeparator(Start, End);
      if (TkEnd)
        return makeState(PS_InRootDir, Start, TkEnd);
      else
        return makeState(PS_InFilenames, Start, consumeName(Start, End));
    }
    case PS_InRootDir:
      return makeState(PS_InFilenames, Start, consumeName(Start, End));

    case PS_InFilenames: {
      PosPtr SepEnd = consumeSeparator(Start, End);
      if (SepEnd != End) {
        PosPtr TkEnd = consumeName(SepEnd, End);
        if (TkEnd)
          return makeState(PS_InFilenames, SepEnd, TkEnd);
      }
      return makeState(PS_InTrailingSep, Start, SepEnd);
    }

    case PS_InTrailingSep:
      return makeState(PS_AtEnd);

    case PS_InRootName:
    case PS_AtEnd:
      _LIBCPP_UNREACHABLE();
    }
  }

  void decrement() noexcept {
    const PosPtr REnd = getBeforeFront();
    const PosPtr RStart = getCurrentTokenStartPos() - 1;
    if (RStart == REnd) // we're decrementing the begin
      return makeState(PS_BeforeBegin);

    switch (State) {
    case PS_AtEnd: {
      // Try to consume a trailing separator or root directory first.
      if (PosPtr SepEnd = consumeSeparator(RStart, REnd)) {
        if (SepEnd == REnd)
          return makeState(PS_InRootDir, Path.data(), RStart + 1);
        return makeState(PS_InTrailingSep, SepEnd + 1, RStart + 1);
      } else {
        PosPtr TkStart = consumeName(RStart, REnd);
        return makeState(PS_InFilenames, TkStart + 1, RStart + 1);
      }
    }
    case PS_InTrailingSep:
      return makeState(PS_InFilenames, consumeName(RStart, REnd) + 1,
                       RStart + 1);
    case PS_InFilenames: {
      PosPtr SepEnd = consumeSeparator(RStart, REnd);
      if (SepEnd == REnd)
        return makeState(PS_InRootDir, Path.data(), RStart + 1);
      PosPtr TkEnd = consumeName(SepEnd, REnd);
      return makeState(PS_InFilenames, TkEnd + 1, SepEnd + 1);
    }
    case PS_InRootDir:
      // return makeState(PS_InRootName, Path.data(), RStart + 1);
    case PS_InRootName:
    case PS_BeforeBegin:
      _LIBCPP_UNREACHABLE();
    }
  }

  /// \brief Return a view with the "preferred representation" of the current
  ///   element. For example trailing separators are represented as a '.'
  string_view_t operator*() const noexcept {
    switch (State) {
    case PS_BeforeBegin:
    case PS_AtEnd:
      return "";
    case PS_InRootDir:
      return "/";
    case PS_InTrailingSep:
      return "";
    case PS_InRootName:
    case PS_InFilenames:
      return RawEntry;
    }
    _LIBCPP_UNREACHABLE();
  }

  explicit operator bool() const noexcept {
    return State != PS_BeforeBegin && State != PS_AtEnd;
  }

  PathParser& operator++() noexcept {
    increment();
    return *this;
  }

  PathParser& operator--() noexcept {
    decrement();
    return *this;
  }

  bool atEnd() const noexcept {
    return State == PS_AtEnd;
  }

  bool inRootDir() const noexcept {
    return State == PS_InRootDir;
  }

  bool inRootName() const noexcept {
    return State == PS_InRootName;
  }

  bool inRootPath() const noexcept {
    return inRootName() || inRootDir();
  }

private:
  void makeState(ParserState NewState, PosPtr Start, PosPtr End) noexcept {
    State = NewState;
    RawEntry = string_view_t(Start, End - Start);
  }
  void makeState(ParserState NewState) noexcept {
    State = NewState;
    RawEntry = {};
  }

  PosPtr getAfterBack() const noexcept { return Path.data() + Path.size(); }

  PosPtr getBeforeFront() const noexcept { return Path.data() - 1; }

  /// \brief Return a pointer to the first character after the currently
  ///   lexed element.
  PosPtr getNextTokenStartPos() const noexcept {
    switch (State) {
    case PS_BeforeBegin:
      return Path.data();
    case PS_InRootName:
    case PS_InRootDir:
    case PS_InFilenames:
      return &RawEntry.back() + 1;
    case PS_InTrailingSep:
    case PS_AtEnd:
      return getAfterBack();
    }
    _LIBCPP_UNREACHABLE();
  }

  /// \brief Return a pointer to the first character in the currently lexed
  ///   element.
  PosPtr getCurrentTokenStartPos() const noexcept {
    switch (State) {
    case PS_BeforeBegin:
    case PS_InRootName:
      return &Path.front();
    case PS_InRootDir:
    case PS_InFilenames:
    case PS_InTrailingSep:
      return &RawEntry.front();
    case PS_AtEnd:
      return &Path.back() + 1;
    }
    _LIBCPP_UNREACHABLE();
  }

  PosPtr consumeSeparator(PosPtr P, PosPtr End) const noexcept {
    if (P == End || *P != '/')
      return nullptr;
    const int Inc = P < End ? 1 : -1;
    P += Inc;
    while (P != End && *P == '/')
      P += Inc;
    return P;
  }

  PosPtr consumeName(PosPtr P, PosPtr End) const noexcept {
    if (P == End || *P == '/')
      return nullptr;
    const int Inc = P < End ? 1 : -1;
    P += Inc;
    while (P != End && *P != '/')
      P += Inc;
    return P;
  }
};

string_view_pair separate_filename(string_view_t const& s) {
  if (s == "." || s == ".." || s.empty())
    return string_view_pair{s, ""};
  auto pos = s.find_last_of('.');
  if (pos == string_view_t::npos || pos == 0)
    return string_view_pair{s, string_view_t{}};
  return string_view_pair{s.substr(0, pos), s.substr(pos)};
}

string_view_t createView(PosPtr S, PosPtr E) noexcept {
  return {S, static_cast<size_t>(E - S) + 1};
}

} // namespace parser
} // namespace

//                       POSIX HELPERS

namespace detail {
namespace {

using value_type = path::value_type;
using string_type = path::string_type;

struct FileDescriptor {
  const path& name;
  int fd = -1;
  StatT m_stat;
  file_status m_status;

  template <class... Args>
  static FileDescriptor create(const path* p, error_code& ec, Args... args) {
    ec.clear();
    int fd;
    if ((fd = ::open(p->c_str(), args...)) == -1) {
      ec = capture_errno();
      return FileDescriptor{p};
    }
    return FileDescriptor(p, fd);
  }

  template <class... Args>
  static FileDescriptor create_with_status(const path* p, error_code& ec,
                                           Args... args) {
    FileDescriptor fd = create(p, ec, args...);
    if (!ec)
      fd.refresh_status(ec);

    return fd;
  }

  file_status get_status() const { return m_status; }
  StatT const& get_stat() const { return m_stat; }

  bool status_known() const { return _VSTD_FS::status_known(m_status); }

  file_status refresh_status(error_code& ec);

  void close() noexcept {
    if (fd != -1)
      ::close(fd);
    fd = -1;
  }

  FileDescriptor(FileDescriptor&& other)
      : name(other.name), fd(other.fd), m_stat(other.m_stat),
        m_status(other.m_status) {
    other.fd = -1;
    other.m_status = file_status{};
  }

  ~FileDescriptor() { close(); }

  FileDescriptor(FileDescriptor const&) = delete;
  FileDescriptor& operator=(FileDescriptor const&) = delete;

private:
  explicit FileDescriptor(const path* p, int fd = -1) : name(*p), fd(fd) {}
};

perms posix_get_perms(const StatT& st) noexcept {
  return static_cast<perms>(st.st_mode) & perms::mask;
}

::mode_t posix_convert_perms(perms prms) {
  return static_cast< ::mode_t>(prms & perms::mask);
}

file_status create_file_status(error_code& m_ec, path const& p,
                               const StatT& path_stat, error_code* ec) {
  if (ec)
    *ec = m_ec;
  if (m_ec && (m_ec.value() == ENOENT || m_ec.value() == ENOTDIR)) {
    return file_status(file_type::not_found);
  } else if (m_ec) {
    ErrorHandler<void> err("posix_stat", ec, &p);
    err.report(m_ec, "failed to determine attributes for the specified path");
    return file_status(file_type::none);
  }
  // else

  file_status fs_tmp;
  auto const mode = path_stat.st_mode;
  if (S_ISLNK(mode))
    fs_tmp.type(file_type::symlink);
  else if (S_ISREG(mode))
    fs_tmp.type(file_type::regular);
  else if (S_ISDIR(mode))
    fs_tmp.type(file_type::directory);
  else if (S_ISBLK(mode))
    fs_tmp.type(file_type::block);
  else if (S_ISCHR(mode))
    fs_tmp.type(file_type::character);
  else if (S_ISFIFO(mode))
    fs_tmp.type(file_type::fifo);
  else if (S_ISSOCK(mode))
    fs_tmp.type(file_type::socket);
  else
    fs_tmp.type(file_type::unknown);

  fs_tmp.permissions(detail::posix_get_perms(path_stat));
  return fs_tmp;
}

file_status posix_stat(path const& p, StatT& path_stat, error_code* ec) {
  error_code m_ec;
  if (::stat(p.c_str(), &path_stat) == -1)
    m_ec = detail::capture_errno();
  return create_file_status(m_ec, p, path_stat, ec);
}

file_status posix_stat(path const& p, error_code* ec) {
  StatT path_stat;
  return posix_stat(p, path_stat, ec);
}

file_status posix_lstat(path const& p, StatT& path_stat, error_code* ec) {
  error_code m_ec;
  if (::lstat(p.c_str(), &path_stat) == -1)
    m_ec = detail::capture_errno();
  return create_file_status(m_ec, p, path_stat, ec);
}

file_status posix_lstat(path const& p, error_code* ec) {
  StatT path_stat;
  return posix_lstat(p, path_stat, ec);
}

// http://pubs.opengroup.org/onlinepubs/9699919799/functions/ftruncate.html
bool posix_ftruncate(const FileDescriptor& fd, off_t to_size, error_code& ec) {
  if (::ftruncate(fd.fd, to_size) == -1) {
    ec = capture_errno();
    return true;
  }
  ec.clear();
  return false;
}

bool posix_fchmod(const FileDescriptor& fd, const StatT& st, error_code& ec) {
  if (::fchmod(fd.fd, st.st_mode) == -1) {
    ec = capture_errno();
    return true;
  }
  ec.clear();
  return false;
}

bool stat_equivalent(const StatT& st1, const StatT& st2) {
  return (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);
}

file_status FileDescriptor::refresh_status(error_code& ec) {
  // FD must be open and good.
  m_status = file_status{};
  m_stat = {};
  error_code m_ec;
  if (::fstat(fd, &m_stat) == -1)
    m_ec = capture_errno();
  m_status = create_file_status(m_ec, name, m_stat, &ec);
  return m_status;
}
} // namespace
} // end namespace detail

using detail::capture_errno;
using detail::ErrorHandler;
using detail::StatT;
using detail::TimeSpec;
using parser::createView;
using parser::PathParser;
using parser::string_view_t;

const bool _FilesystemClock::is_steady;

_FilesystemClock::time_point _FilesystemClock::now() noexcept {
  typedef chrono::duration<rep> __secs;
#if defined(_LIBCPP_USE_CLOCK_GETTIME) && defined(CLOCK_REALTIME)
  typedef chrono::duration<rep, nano> __nsecs;
  struct timespec tp;
  if (0 != clock_gettime(CLOCK_REALTIME, &tp))
    __throw_system_error(errno, "clock_gettime(CLOCK_REALTIME) failed");
  return time_point(__secs(tp.tv_sec) +
                    chrono::duration_cast<duration>(__nsecs(tp.tv_nsec)));
#else
  typedef chrono::duration<rep, micro> __microsecs;
  timeval tv;
  gettimeofday(&tv, 0);
  return time_point(__secs(tv.tv_sec) + __microsecs(tv.tv_usec));
#endif // _LIBCPP_USE_CLOCK_GETTIME && CLOCK_REALTIME
}

filesystem_error::~filesystem_error() {}

void filesystem_error::__create_what(int __num_paths) {
  const char* derived_what = system_error::what();
  __storage_->__what_ = [&]() -> string {
    const char* p1 = path1().native().empty() ? "\"\"" : path1().c_str();
    const char* p2 = path2().native().empty() ? "\"\"" : path2().c_str();
    switch (__num_paths) {
    default:
      return detail::format_string("filesystem error: %s", derived_what);
    case 1:
      return detail::format_string("filesystem error: %s [%s]", derived_what,
                                   p1);
    case 2:
      return detail::format_string("filesystem error: %s [%s] [%s]",
                                   derived_what, p1, p2);
    }
  }();
}

static path __do_absolute(const path& p, path* cwd, error_code* ec) {
  if (ec)
    ec->clear();
  if (p.is_absolute())
    return p;
  *cwd = __current_path(ec);
  if (ec && *ec)
    return {};
  return (*cwd) / p;
}

path __absolute(const path& p, error_code* ec) {
  path cwd;
  return __do_absolute(p, &cwd, ec);
}

path __canonical(path const& orig_p, error_code* ec) {
  path cwd;
  ErrorHandler<path> err("canonical", ec, &orig_p, &cwd);

  path p = __do_absolute(orig_p, &cwd, ec);
  char buff[PATH_MAX + 1];
  char* ret;
  if ((ret = ::realpath(p.c_str(), buff)) == nullptr)
    return err.report(capture_errno());
  return {ret};
}

void __copy(const path& from, const path& to, copy_options options,
            error_code* ec) {
  ErrorHandler<void> err("copy", ec, &from, &to);

  const bool sym_status = bool(
      options & (copy_options::create_symlinks | copy_options::skip_symlinks));

  const bool sym_status2 = bool(options & copy_options::copy_symlinks);

  error_code m_ec1;
  StatT f_st = {};
  const file_status f = sym_status || sym_status2
                            ? detail::posix_lstat(from, f_st, &m_ec1)
                            : detail::posix_stat(from, f_st, &m_ec1);
  if (m_ec1)
    return err.report(m_ec1);

  StatT t_st = {};
  const file_status t = sym_status ? detail::posix_lstat(to, t_st, &m_ec1)
                                   : detail::posix_stat(to, t_st, &m_ec1);

  if (not status_known(t))
    return err.report(m_ec1);

  if (!exists(f) || is_other(f) || is_other(t) ||
      (is_directory(f) && is_regular_file(t)) ||
      detail::stat_equivalent(f_st, t_st)) {
    return err.report(errc::function_not_supported);
  }

  if (ec)
    ec->clear();

  if (is_symlink(f)) {
    if (bool(copy_options::skip_symlinks & options)) {
      // do nothing
    } else if (not exists(t)) {
      __copy_symlink(from, to, ec);
    } else {
      return err.report(errc::file_exists);
    }
    return;
  } else if (is_regular_file(f)) {
    if (bool(copy_options::directories_only & options)) {
      // do nothing
    } else if (bool(copy_options::create_symlinks & options)) {
      __create_symlink(from, to, ec);
    } else if (bool(copy_options::create_hard_links & options)) {
      __create_hard_link(from, to, ec);
    } else if (is_directory(t)) {
      __copy_file(from, to / from.filename(), options, ec);
    } else {
      __copy_file(from, to, options, ec);
    }
    return;
  } else if (is_directory(f) && bool(copy_options::create_symlinks & options)) {
    return err.report(errc::is_a_directory);
  } else if (is_directory(f) && (bool(copy_options::recursive & options) ||
                                 copy_options::none == options)) {

    if (!exists(t)) {
      // create directory to with attributes from 'from'.
      __create_directory(to, from, ec);
      if (ec && *ec) {
        return;
      }
    }
    directory_iterator it =
        ec ? directory_iterator(from, *ec) : directory_iterator(from);
    if (ec && *ec) {
      return;
    }
    error_code m_ec2;
    for (; it != directory_iterator(); it.increment(m_ec2)) {
      if (m_ec2) {
        return err.report(m_ec2);
      }
      __copy(it->path(), to / it->path().filename(),
             options | copy_options::__in_recursive_copy, ec);
      if (ec && *ec) {
        return;
      }
    }
  }
}

namespace detail {
namespace {

#ifdef _LIBCPP_USE_SENDFILE
bool copy_file_impl_sendfile(FileDescriptor& read_fd, FileDescriptor& write_fd,
                             error_code& ec) {

  size_t count = read_fd.get_stat().st_size;
  do {
    ssize_t res;
    if ((res = ::sendfile(write_fd.fd, read_fd.fd, nullptr, count)) == -1) {
      ec = capture_errno();
      return false;
    }
    count -= res;
  } while (count > 0);

  ec.clear();

  return true;
}
#elif defined(_LIBCPP_USE_COPYFILE)
bool copy_file_impl_copyfile(FileDescriptor& read_fd, FileDescriptor& write_fd,
                             error_code& ec) {
  struct CopyFileState {
    copyfile_state_t state;
    CopyFileState() { state = copyfile_state_alloc(); }
    ~CopyFileState() { copyfile_state_free(state); }

  private:
    CopyFileState(CopyFileState const&) = delete;
    CopyFileState& operator=(CopyFileState const&) = delete;
  };

  CopyFileState cfs;
  if (fcopyfile(read_fd.fd, write_fd.fd, cfs.state, COPYFILE_DATA) < 0) {
    ec = capture_errno();
    return false;
  }

  ec.clear();
  return true;
}
#endif

// Note: This function isn't guarded by ifdef's even though it may be unused
// in order to assure it still compiles.
__attribute__((unused)) bool copy_file_impl_default(FileDescriptor& read_fd,
                                                    FileDescriptor& write_fd,
                                                    error_code& ec) {
  ifstream in;
  in.__open(read_fd.fd, ios::binary);
  if (!in.is_open()) {
    // This assumes that __open didn't reset the error code.
    ec = capture_errno();
    return false;
  }
  ofstream out;
  out.__open(write_fd.fd, ios::binary);
  if (!out.is_open()) {
    ec = capture_errno();
    return false;
  }

  if (in.good() && out.good()) {
    using InIt = istreambuf_iterator<char>;
    using OutIt = ostreambuf_iterator<char>;
    InIt bin(in);
    InIt ein;
    OutIt bout(out);
    copy(bin, ein, bout);
  }
  if (out.fail() || in.fail()) {
    ec = make_error_code(errc::io_error);
    return false;
  }

  ec.clear();
  return true;
}

bool copy_file_impl(FileDescriptor& from, FileDescriptor& to, error_code& ec) {
#if defined(_LIBCPP_USE_SENDFILE)
  return copy_file_impl_sendfile(from, to, ec);
#elif defined(_LIBCPP_USE_COPYFILE)
  return copy_file_impl_copyfile(from, to, ec);
#else
  return copy_file_impl_default(from, to, ec);
#endif
}

} // namespace
} // namespace detail

bool __copy_file(const path& from, const path& to, copy_options options,
                 error_code* ec) {
  using detail::FileDescriptor;
  ErrorHandler<bool> err("copy_file", ec, &to, &from);

  error_code m_ec;
  FileDescriptor from_fd =
      FileDescriptor::create_with_status(&from, m_ec, O_RDONLY | O_NONBLOCK);
  if (m_ec)
    return err.report(m_ec);

  auto from_st = from_fd.get_status();
  StatT const& from_stat = from_fd.get_stat();
  if (!is_regular_file(from_st)) {
    if (not m_ec)
      m_ec = make_error_code(errc::not_supported);
    return err.report(m_ec);
  }

  const bool skip_existing = bool(copy_options::skip_existing & options);
  const bool update_existing = bool(copy_options::update_existing & options);
  const bool overwrite_existing =
      bool(copy_options::overwrite_existing & options);

  StatT to_stat_path;
  file_status to_st = detail::posix_stat(to, to_stat_path, &m_ec);
  if (!status_known(to_st))
    return err.report(m_ec);

  const bool to_exists = exists(to_st);
  if (to_exists && !is_regular_file(to_st))
    return err.report(errc::not_supported);

  if (to_exists && detail::stat_equivalent(from_stat, to_stat_path))
    return err.report(errc::file_exists);

  if (to_exists && skip_existing)
    return false;

  bool ShouldCopy = [&]() {
    if (to_exists && update_existing) {
      auto from_time = detail::extract_mtime(from_stat);
      auto to_time = detail::extract_mtime(to_stat_path);
      if (from_time.tv_sec < to_time.tv_sec)
        return false;
      if (from_time.tv_sec == to_time.tv_sec &&
          from_time.tv_nsec <= to_time.tv_nsec)
        return false;
      return true;
    }
    if (!to_exists || overwrite_existing)
      return true;
    return err.report(errc::file_exists);
  }();
  if (!ShouldCopy)
    return false;

  // Don't truncate right away. We may not be opening the file we originally
  // looked at; we'll check this later.
  int to_open_flags = O_WRONLY;
  if (!to_exists)
    to_open_flags |= O_CREAT;
  FileDescriptor to_fd = FileDescriptor::create_with_status(
      &to, m_ec, to_open_flags, from_stat.st_mode);
  if (m_ec)
    return err.report(m_ec);

  if (to_exists) {
    // Check that the file we initially stat'ed is equivalent to the one
    // we opened.
    // FIXME: report this better.
    if (!detail::stat_equivalent(to_stat_path, to_fd.get_stat()))
      return err.report(errc::bad_file_descriptor);

    // Set the permissions and truncate the file we opened.
    if (detail::posix_fchmod(to_fd, from_stat, m_ec))
      return err.report(m_ec);
    if (detail::posix_ftruncate(to_fd, 0, m_ec))
      return err.report(m_ec);
  }

  if (!copy_file_impl(from_fd, to_fd, m_ec)) {
    // FIXME: Remove the dest file if we failed, and it didn't exist previously.
    return err.report(m_ec);
  }

  return true;
}

void __copy_symlink(const path& existing_symlink, const path& new_symlink,
                    error_code* ec) {
  const path real_path(__read_symlink(existing_symlink, ec));
  if (ec && *ec) {
    return;
  }
  // NOTE: proposal says you should detect if you should call
  // create_symlink or create_directory_symlink. I don't think this
  // is needed with POSIX
  __create_symlink(real_path, new_symlink, ec);
}

bool __create_directories(const path& p, error_code* ec) {
  ErrorHandler<bool> err("create_directories", ec, &p);

  error_code m_ec;
  auto const st = detail::posix_stat(p, &m_ec);
  if (!status_known(st))
    return err.report(m_ec);
  else if (is_directory(st))
    return false;
  else if (exists(st))
    return err.report(errc::file_exists);

  const path parent = p.parent_path();
  if (!parent.empty()) {
    const file_status parent_st = status(parent, m_ec);
    if (not status_known(parent_st))
      return err.report(m_ec);
    if (not exists(parent_st)) {
      __create_directories(parent, ec);
      if (ec && *ec) {
        return false;
      }
    }
  }
  return __create_directory(p, ec);
}

bool __create_directory(const path& p, error_code* ec) {
  ErrorHandler<bool> err("create_directory", ec, &p);

  if (::mkdir(p.c_str(), static_cast<int>(perms::all)) == 0)
    return true;
  if (errno != EEXIST)
    err.report(capture_errno());
  return false;
}

bool __create_directory(path const& p, path const& attributes, error_code* ec) {
  ErrorHandler<bool> err("create_directory", ec, &p, &attributes);

  StatT attr_stat;
  error_code mec;
  auto st = detail::posix_stat(attributes, attr_stat, &mec);
  if (!status_known(st))
    return err.report(mec);
  if (!is_directory(st))
    return err.report(errc::not_a_directory,
                      "the specified attribute path is invalid");

  if (::mkdir(p.c_str(), attr_stat.st_mode) == 0)
    return true;
  if (errno != EEXIST)
    err.report(capture_errno());
  return false;
}

void __create_directory_symlink(path const& from, path const& to,
                                error_code* ec) {
  ErrorHandler<void> err("create_directory_symlink", ec, &from, &to);
  if (::symlink(from.c_str(), to.c_str()) != 0)
    return err.report(capture_errno());
}

void __create_hard_link(const path& from, const path& to, error_code* ec) {
  ErrorHandler<void> err("create_hard_link", ec, &from, &to);
  if (::link(from.c_str(), to.c_str()) == -1)
    return err.report(capture_errno());
}

void __create_symlink(path const& from, path const& to, error_code* ec) {
  ErrorHandler<void> err("create_symlink", ec, &from, &to);
  if (::symlink(from.c_str(), to.c_str()) == -1)
    return err.report(capture_errno());
}

path __current_path(error_code* ec) {
  ErrorHandler<path> err("current_path", ec);

  auto size = ::pathconf(".", _PC_PATH_MAX);
  _LIBCPP_ASSERT(size >= 0, "pathconf returned a 0 as max size");

  auto buff = unique_ptr<char[]>(new char[size + 1]);
  char* ret;
  if ((ret = ::getcwd(buff.get(), static_cast<size_t>(size))) == nullptr)
    return err.report(capture_errno(), "call to getcwd failed");

  return {buff.get()};
}

void __current_path(const path& p, error_code* ec) {
  ErrorHandler<void> err("current_path", ec, &p);
  if (::chdir(p.c_str()) == -1)
    err.report(capture_errno());
}

bool __equivalent(const path& p1, const path& p2, error_code* ec) {
  ErrorHandler<bool> err("equivalent", ec, &p1, &p2);

  error_code ec1, ec2;
  StatT st1 = {}, st2 = {};
  auto s1 = detail::posix_stat(p1.native(), st1, &ec1);
  if (!exists(s1))
    return err.report(errc::not_supported);
  auto s2 = detail::posix_stat(p2.native(), st2, &ec2);
  if (!exists(s2))
    return err.report(errc::not_supported);

  return detail::stat_equivalent(st1, st2);
}

uintmax_t __file_size(const path& p, error_code* ec) {
  ErrorHandler<uintmax_t> err("file_size", ec, &p);

  error_code m_ec;
  StatT st;
  file_status fst = detail::posix_stat(p, st, &m_ec);
  if (!exists(fst) || !is_regular_file(fst)) {
    errc error_kind =
        is_directory(fst) ? errc::is_a_directory : errc::not_supported;
    if (!m_ec)
      m_ec = make_error_code(error_kind);
    return err.report(m_ec);
  }
  // is_regular_file(p) == true
  return static_cast<uintmax_t>(st.st_size);
}

uintmax_t __hard_link_count(const path& p, error_code* ec) {
  ErrorHandler<uintmax_t> err("hard_link_count", ec, &p);

  error_code m_ec;
  StatT st;
  detail::posix_stat(p, st, &m_ec);
  if (m_ec)
    return err.report(m_ec);
  return static_cast<uintmax_t>(st.st_nlink);
}

bool __fs_is_empty(const path& p, error_code* ec) {
  ErrorHandler<bool> err("is_empty", ec, &p);

  error_code m_ec;
  StatT pst;
  auto st = detail::posix_stat(p, pst, &m_ec);
  if (m_ec)
    return err.report(m_ec);
  else if (!is_directory(st) && !is_regular_file(st))
    return err.report(errc::not_supported);
  else if (is_directory(st)) {
    auto it = ec ? directory_iterator(p, *ec) : directory_iterator(p);
    if (ec && *ec)
      return false;
    return it == directory_iterator{};
  } else if (is_regular_file(st))
    return static_cast<uintmax_t>(pst.st_size) == 0;

  _LIBCPP_UNREACHABLE();
}

static file_time_type __extract_last_write_time(const path& p, const StatT& st,
                                                error_code* ec) {
  using detail::fs_time;
  ErrorHandler<file_time_type> err("last_write_time", ec, &p);

  auto ts = detail::extract_mtime(st);
  if (!fs_time::is_representable(ts))
    return err.report(errc::value_too_large);

  return fs_time::convert_from_timespec(ts);
}

file_time_type __last_write_time(const path& p, error_code* ec) {
  using namespace chrono;
  ErrorHandler<file_time_type> err("last_write_time", ec, &p);

  error_code m_ec;
  StatT st;
  detail::posix_stat(p, st, &m_ec);
  if (m_ec)
    return err.report(m_ec);
  return __extract_last_write_time(p, st, ec);
}

void __last_write_time(const path& p, file_time_type new_time, error_code* ec) {
  using detail::fs_time;
  ErrorHandler<void> err("last_write_time", ec, &p);

  error_code m_ec;
  array<TimeSpec, 2> tbuf;
#if !defined(_LIBCPP_USE_UTIMENSAT)
  // This implementation has a race condition between determining the
  // last access time and attempting to set it to the same value using
  // ::utimes
  StatT st;
  file_status fst = detail::posix_stat(p, st, &m_ec);
  if (m_ec)
    return err.report(m_ec);
  tbuf[0] = detail::extract_atime(st);
#else
  tbuf[0].tv_sec = 0;
  tbuf[0].tv_nsec = UTIME_OMIT;
#endif
  if (!fs_time::convert_to_timespec(tbuf[1], new_time))
    return err.report(errc::value_too_large);

  detail::set_file_times(p, tbuf, m_ec);
  if (m_ec)
    return err.report(m_ec);
}

void __permissions(const path& p, perms prms, perm_options opts,
                   error_code* ec) {
  ErrorHandler<void> err("permissions", ec, &p);

  auto has_opt = [&](perm_options o) { return bool(o & opts); };
  const bool resolve_symlinks = !has_opt(perm_options::nofollow);
  const bool add_perms = has_opt(perm_options::add);
  const bool remove_perms = has_opt(perm_options::remove);
  _LIBCPP_ASSERT(
      (add_perms + remove_perms + has_opt(perm_options::replace)) == 1,
      "One and only one of the perm_options constants replace, add, or remove "
      "is present in opts");

  bool set_sym_perms = false;
  prms &= perms::mask;
  if (!resolve_symlinks || (add_perms || remove_perms)) {
    error_code m_ec;
    file_status st = resolve_symlinks ? detail::posix_stat(p, &m_ec)
                                      : detail::posix_lstat(p, &m_ec);
    set_sym_perms = is_symlink(st);
    if (m_ec)
      return err.report(m_ec);
    _LIBCPP_ASSERT(st.permissions() != perms::unknown,
                   "Permissions unexpectedly unknown");
    if (add_perms)
      prms |= st.permissions();
    else if (remove_perms)
      prms = st.permissions() & ~prms;
  }
  const auto real_perms = detail::posix_convert_perms(prms);

#if defined(AT_SYMLINK_NOFOLLOW) && defined(AT_FDCWD)
  const int flags = set_sym_perms ? AT_SYMLINK_NOFOLLOW : 0;
  if (::fchmodat(AT_FDCWD, p.c_str(), real_perms, flags) == -1) {
    return err.report(capture_errno());
  }
#else
  if (set_sym_perms)
    return err.report(errc::operation_not_supported);
  if (::chmod(p.c_str(), real_perms) == -1) {
    return err.report(capture_errno());
  }
#endif
}

path __read_symlink(const path& p, error_code* ec) {
  ErrorHandler<path> err("read_symlink", ec, &p);

  char buff[PATH_MAX + 1];
  error_code m_ec;
  ::ssize_t ret;
  if ((ret = ::readlink(p.c_str(), buff, PATH_MAX)) == -1) {
    return err.report(capture_errno());
  }
  _LIBCPP_ASSERT(ret <= PATH_MAX, "TODO");
  _LIBCPP_ASSERT(ret > 0, "TODO");
  buff[ret] = 0;
  return {buff};
}

bool __remove(const path& p, error_code* ec) {
  ErrorHandler<bool> err("remove", ec, &p);
  if (::remove(p.c_str()) == -1) {
    if (errno != ENOENT)
      err.report(capture_errno());
    return false;
  }
  return true;
}

namespace {

uintmax_t remove_all_impl(path const& p, error_code& ec) {
  const auto npos = static_cast<uintmax_t>(-1);
  const file_status st = __symlink_status(p, &ec);
  if (ec)
    return npos;
  uintmax_t count = 1;
  if (is_directory(st)) {
    for (directory_iterator it(p, ec); !ec && it != directory_iterator();
         it.increment(ec)) {
      auto other_count = remove_all_impl(it->path(), ec);
      if (ec)
        return npos;
      count += other_count;
    }
    if (ec)
      return npos;
  }
  if (!__remove(p, &ec))
    return npos;
  return count;
}

} // end namespace

uintmax_t __remove_all(const path& p, error_code* ec) {
  ErrorHandler<uintmax_t> err("remove_all", ec, &p);

  error_code mec;
  auto count = remove_all_impl(p, mec);
  if (mec) {
    if (mec == errc::no_such_file_or_directory)
      return 0;
    return err.report(mec);
  }
  return count;
}

void __rename(const path& from, const path& to, error_code* ec) {
  ErrorHandler<void> err("rename", ec, &from, &to);
  if (::rename(from.c_str(), to.c_str()) == -1)
    err.report(capture_errno());
}

void __resize_file(const path& p, uintmax_t size, error_code* ec) {
  ErrorHandler<void> err("resize_file", ec, &p);
  if (::truncate(p.c_str(), static_cast< ::off_t>(size)) == -1)
    return err.report(capture_errno());
}

space_info __space(const path& p, error_code* ec) {
  ErrorHandler<void> err("space", ec, &p);
  space_info si;
  struct statvfs m_svfs = {};
  if (::statvfs(p.c_str(), &m_svfs) == -1) {
    err.report(capture_errno());
    si.capacity = si.free = si.available = static_cast<uintmax_t>(-1);
    return si;
  }
  // Multiply with overflow checking.
  auto do_mult = [&](uintmax_t& out, uintmax_t other) {
    out = other * m_svfs.f_frsize;
    if (other == 0 || out / other != m_svfs.f_frsize)
      out = static_cast<uintmax_t>(-1);
  };
  do_mult(si.capacity, m_svfs.f_blocks);
  do_mult(si.free, m_svfs.f_bfree);
  do_mult(si.available, m_svfs.f_bavail);
  return si;
}

file_status __status(const path& p, error_code* ec) {
  return detail::posix_stat(p, ec);
}

file_status __symlink_status(const path& p, error_code* ec) {
  return detail::posix_lstat(p, ec);
}

path __temp_directory_path(error_code* ec) {
  ErrorHandler<path> err("temp_directory_path", ec);

  const char* env_paths[] = {"TMPDIR", "TMP", "TEMP", "TEMPDIR"};
  const char* ret = nullptr;

  for (auto& ep : env_paths)
    if ((ret = getenv(ep)))
      break;
  if (ret == nullptr)
    ret = "/tmp";

  path p(ret);
  error_code m_ec;
  file_status st = detail::posix_stat(p, &m_ec);
  if (!status_known(st))
    return err.report(m_ec, "cannot access path \"%s\"", p);

  if (!exists(st) || !is_directory(st))
    return err.report(errc::not_a_directory, "path \"%s\" is not a directory",
                      p);

  return p;
}

path __weakly_canonical(const path& p, error_code* ec) {
  ErrorHandler<path> err("weakly_canonical", ec, &p);

  if (p.empty())
    return __canonical("", ec);

  path result;
  path tmp;
  tmp.__reserve(p.native().size());
  auto PP = PathParser::CreateEnd(p.native());
  --PP;
  vector<string_view_t> DNEParts;

  while (PP.State != PathParser::PS_BeforeBegin) {
    tmp.assign(createView(p.native().data(), &PP.RawEntry.back()));
    error_code m_ec;
    file_status st = __status(tmp, &m_ec);
    if (!status_known(st)) {
      return err.report(m_ec);
    } else if (exists(st)) {
      result = __canonical(tmp, ec);
      break;
    }
    DNEParts.push_back(*PP);
    --PP;
  }
  if (PP.State == PathParser::PS_BeforeBegin)
    result = __canonical("", ec);
  if (ec)
    ec->clear();
  if (DNEParts.empty())
    return result;
  for (auto It = DNEParts.rbegin(); It != DNEParts.rend(); ++It)
    result /= *It;
  return result.lexically_normal();
}

///////////////////////////////////////////////////////////////////////////////
//                            path definitions
///////////////////////////////////////////////////////////////////////////////

constexpr path::value_type path::preferred_separator;

path& path::replace_extension(path const& replacement) {
  path p = extension();
  if (not p.empty()) {
    __pn_.erase(__pn_.size() - p.native().size());
  }
  if (!replacement.empty()) {
    if (replacement.native()[0] != '.') {
      __pn_ += ".";
    }
    __pn_.append(replacement.__pn_);
  }
  return *this;
}

///////////////////////////////////////////////////////////////////////////////
// path.decompose

string_view_t path::__root_name() const {
  auto PP = PathParser::CreateBegin(__pn_);
  if (PP.State == PathParser::PS_InRootName)
    return *PP;
  return {};
}

string_view_t path::__root_directory() const {
  auto PP = PathParser::CreateBegin(__pn_);
  if (PP.State == PathParser::PS_InRootName)
    ++PP;
  if (PP.State == PathParser::PS_InRootDir)
    return *PP;
  return {};
}

string_view_t path::__root_path_raw() const {
  auto PP = PathParser::CreateBegin(__pn_);
  if (PP.State == PathParser::PS_InRootName) {
    auto NextCh = PP.peek();
    if (NextCh && *NextCh == '/') {
      ++PP;
      return createView(__pn_.data(), &PP.RawEntry.back());
    }
    return PP.RawEntry;
  }
  if (PP.State == PathParser::PS_InRootDir)
    return *PP;
  return {};
}

static bool ConsumeRootName(PathParser *PP) {
  static_assert(PathParser::PS_BeforeBegin == 1 &&
      PathParser::PS_InRootName == 2,
      "Values for enums are incorrect");
  while (PP->State <= PathParser::PS_InRootName)
    ++(*PP);
  return PP->State == PathParser::PS_AtEnd;
}

static bool ConsumeRootDir(PathParser* PP) {
  static_assert(PathParser::PS_BeforeBegin == 1 &&
                PathParser::PS_InRootName == 2 &&
                PathParser::PS_InRootDir == 3, "Values for enums are incorrect");
  while (PP->State <= PathParser::PS_InRootDir)
    ++(*PP);
  return PP->State == PathParser::PS_AtEnd;
}

string_view_t path::__relative_path() const {
  auto PP = PathParser::CreateBegin(__pn_);
  if (ConsumeRootDir(&PP))
    return {};
  return createView(PP.RawEntry.data(), &__pn_.back());
}

string_view_t path::__parent_path() const {
  if (empty())
    return {};
  // Determine if we have a root path but not a relative path. In that case
  // return *this.
  {
    auto PP = PathParser::CreateBegin(__pn_);
    if (ConsumeRootDir(&PP))
      return __pn_;
  }
  // Otherwise remove a single element from the end of the path, and return
  // a string representing that path
  {
    auto PP = PathParser::CreateEnd(__pn_);
    --PP;
    if (PP.RawEntry.data() == __pn_.data())
      return {};
    --PP;
    return createView(__pn_.data(), &PP.RawEntry.back());
  }
}

string_view_t path::__filename() const {
  if (empty())
    return {};
  {
    PathParser PP = PathParser::CreateBegin(__pn_);
    if (ConsumeRootDir(&PP))
      return {};
  }
  return *(--PathParser::CreateEnd(__pn_));
}

string_view_t path::__stem() const {
  return parser::separate_filename(__filename()).first;
}

string_view_t path::__extension() const {
  return parser::separate_filename(__filename()).second;
}

////////////////////////////////////////////////////////////////////////////
// path.gen

enum PathPartKind : unsigned char {
  PK_None,
  PK_RootSep,
  PK_Filename,
  PK_Dot,
  PK_DotDot,
  PK_TrailingSep
};

static PathPartKind ClassifyPathPart(string_view_t Part) {
  if (Part.empty())
    return PK_TrailingSep;
  if (Part == ".")
    return PK_Dot;
  if (Part == "..")
    return PK_DotDot;
  if (Part == "/")
    return PK_RootSep;
  return PK_Filename;
}

path path::lexically_normal() const {
  if (__pn_.empty())
    return *this;

  using PartKindPair = pair<string_view_t, PathPartKind>;
  vector<PartKindPair> Parts;
  // Guess as to how many elements the path has to avoid reallocating.
  Parts.reserve(32);

  // Track the total size of the parts as we collect them. This allows the
  // resulting path to reserve the correct amount of memory.
  size_t NewPathSize = 0;
  auto AddPart = [&](PathPartKind K, string_view_t P) {
    NewPathSize += P.size();
    Parts.emplace_back(P, K);
  };
  auto LastPartKind = [&]() {
    if (Parts.empty())
      return PK_None;
    return Parts.back().second;
  };

  bool MaybeNeedTrailingSep = false;
  // Build a stack containing the remaining elements of the path, popping off
  // elements which occur before a '..' entry.
  for (auto PP = PathParser::CreateBegin(__pn_); PP; ++PP) {
    auto Part = *PP;
    PathPartKind Kind = ClassifyPathPart(Part);
    switch (Kind) {
    case PK_Filename:
    case PK_RootSep: {
      // Add all non-dot and non-dot-dot elements to the stack of elements.
      AddPart(Kind, Part);
      MaybeNeedTrailingSep = false;
      break;
    }
    case PK_DotDot: {
      // Only push a ".." element if there are no elements preceding the "..",
      // or if the preceding element is itself "..".
      auto LastKind = LastPartKind();
      if (LastKind == PK_Filename) {
        NewPathSize -= Parts.back().first.size();
        Parts.pop_back();
      } else if (LastKind != PK_RootSep)
        AddPart(PK_DotDot, "..");
      MaybeNeedTrailingSep = LastKind == PK_Filename;
      break;
    }
    case PK_Dot:
    case PK_TrailingSep: {
      MaybeNeedTrailingSep = true;
      break;
    }
    case PK_None:
      _LIBCPP_UNREACHABLE();
    }
  }
  // [fs.path.generic]p6.8: If the path is empty, add a dot.
  if (Parts.empty())
    return ".";

  // [fs.path.generic]p6.7: If the last filename is dot-dot, remove any
  // trailing directory-separator.
  bool NeedTrailingSep = MaybeNeedTrailingSep && LastPartKind() == PK_Filename;

  path Result;
  Result.__pn_.reserve(Parts.size() + NewPathSize + NeedTrailingSep);
  for (auto& PK : Parts)
    Result /= PK.first;

  if (NeedTrailingSep)
    Result /= "";

  return Result;
}

static int DetermineLexicalElementCount(PathParser PP) {
  int Count = 0;
  for (; PP; ++PP) {
    auto Elem = *PP;
    if (Elem == "..")
      --Count;
    else if (Elem != "." && Elem != "")
      ++Count;
  }
  return Count;
}

path path::lexically_relative(const path& base) const {
  { // perform root-name/root-directory mismatch checks
    auto PP = PathParser::CreateBegin(__pn_);
    auto PPBase = PathParser::CreateBegin(base.__pn_);
    auto CheckIterMismatchAtBase = [&]() {
      return PP.State != PPBase.State &&
             (PP.inRootPath() || PPBase.inRootPath());
    };
    if (PP.inRootName() && PPBase.inRootName()) {
      if (*PP != *PPBase)
        return {};
    } else if (CheckIterMismatchAtBase())
      return {};

    if (PP.inRootPath())
      ++PP;
    if (PPBase.inRootPath())
      ++PPBase;
    if (CheckIterMismatchAtBase())
      return {};
  }

  // Find the first mismatching element
  auto PP = PathParser::CreateBegin(__pn_);
  auto PPBase = PathParser::CreateBegin(base.__pn_);
  while (PP && PPBase && PP.State == PPBase.State && *PP == *PPBase) {
    ++PP;
    ++PPBase;
  }

  // If there is no mismatch, return ".".
  if (!PP && !PPBase)
    return ".";

  // Otherwise, determine the number of elements, 'n', which are not dot or
  // dot-dot minus the number of dot-dot elements.
  int ElemCount = DetermineLexicalElementCount(PPBase);
  if (ElemCount < 0)
    return {};

  // if n == 0 and (a == end() || a->empty()), returns path("."); otherwise
  if (ElemCount == 0 && (PP.atEnd() || *PP == ""))
    return ".";

  // return a path constructed with 'n' dot-dot elements, followed by the the
  // elements of '*this' after the mismatch.
  path Result;
  // FIXME: Reserve enough room in Result that it won't have to re-allocate.
  while (ElemCount--)
    Result /= "..";
  for (; PP; ++PP)
    Result /= *PP;
  return Result;
}

////////////////////////////////////////////////////////////////////////////
// path.comparisons
static int CompareRootName(PathParser *LHS, PathParser *RHS) {
  if (!LHS->inRootName() && !RHS->inRootName())
    return 0;

  auto GetRootName = [](PathParser *Parser) -> string_view_t {
    return Parser->inRootName() ? **Parser : "";
  };
  int res = GetRootName(LHS).compare(GetRootName(RHS));
  ConsumeRootName(LHS);
  ConsumeRootName(RHS);
  return res;
}

static int CompareRootDir(PathParser *LHS, PathParser *RHS) {
  if (!LHS->inRootDir() && RHS->inRootDir())
    return -1;
  else if (LHS->inRootDir() && !RHS->inRootDir())
    return 1;
  else {
    ConsumeRootDir(LHS);
    ConsumeRootDir(RHS);
    return 0;
  }
}

static int CompareRelative(PathParser *LHSPtr, PathParser *RHSPtr) {
  auto &LHS = *LHSPtr;
  auto &RHS = *RHSPtr;
  
  int res;
  while (LHS && RHS) {
    if ((res = (*LHS).compare(*RHS)) != 0)
      return res;
    ++LHS;
    ++RHS;
  }
  return 0;
}

static int CompareEndState(PathParser *LHS, PathParser *RHS) {
  if (LHS->atEnd() && !RHS->atEnd())
    return -1;
  else if (!LHS->atEnd() && RHS->atEnd())
    return 1;
  return 0;
}

int path::__compare(string_view_t __s) const {
  auto LHS = PathParser::CreateBegin(__pn_);
  auto RHS = PathParser::CreateBegin(__s);
  int res;

  if ((res = CompareRootName(&LHS, &RHS)) != 0)
    return res;

  if ((res = CompareRootDir(&LHS, &RHS)) != 0)
    return res;

  if ((res = CompareRelative(&LHS, &RHS)) != 0)
    return res;

  return CompareEndState(&LHS, &RHS);
}

////////////////////////////////////////////////////////////////////////////
// path.nonmembers
size_t hash_value(const path& __p) noexcept {
  auto PP = PathParser::CreateBegin(__p.native());
  size_t hash_value = 0;
  hash<string_view_t> hasher;
  while (PP) {
    hash_value = __hash_combine(hash_value, hasher(*PP));
    ++PP;
  }
  return hash_value;
}

////////////////////////////////////////////////////////////////////////////
// path.itr
path::iterator path::begin() const {
  auto PP = PathParser::CreateBegin(__pn_);
  iterator it;
  it.__path_ptr_ = this;
  it.__state_ = static_cast<path::iterator::_ParserState>(PP.State);
  it.__entry_ = PP.RawEntry;
  it.__stashed_elem_.__assign_view(*PP);
  return it;
}

path::iterator path::end() const {
  iterator it{};
  it.__state_ = path::iterator::_AtEnd;
  it.__path_ptr_ = this;
  return it;
}

path::iterator& path::iterator::__increment() {
  PathParser PP(__path_ptr_->native(), __entry_, __state_);
  ++PP;
  __state_ = static_cast<_ParserState>(PP.State);
  __entry_ = PP.RawEntry;
  __stashed_elem_.__assign_view(*PP);
  return *this;
}

path::iterator& path::iterator::__decrement() {
  PathParser PP(__path_ptr_->native(), __entry_, __state_);
  --PP;
  __state_ = static_cast<_ParserState>(PP.State);
  __entry_ = PP.RawEntry;
  __stashed_elem_.__assign_view(*PP);
  return *this;
}

///////////////////////////////////////////////////////////////////////////////
//                           directory entry definitions
///////////////////////////////////////////////////////////////////////////////

#ifndef _LIBCPP_WIN32API
error_code directory_entry::__do_refresh() noexcept {
  __data_.__reset();
  error_code failure_ec;

  StatT full_st;
  file_status st = detail::posix_lstat(__p_, full_st, &failure_ec);
  if (!status_known(st)) {
    __data_.__reset();
    return failure_ec;
  }

  if (!_VSTD_FS::exists(st) || !_VSTD_FS::is_symlink(st)) {
    __data_.__cache_type_ = directory_entry::_RefreshNonSymlink;
    __data_.__type_ = st.type();
    __data_.__non_sym_perms_ = st.permissions();
  } else { // we have a symlink
    __data_.__sym_perms_ = st.permissions();
    // Get the information about the linked entity.
    // Ignore errors from stat, since we don't want errors regarding symlink
    // resolution to be reported to the user.
    error_code ignored_ec;
    st = detail::posix_stat(__p_, full_st, &ignored_ec);

    __data_.__type_ = st.type();
    __data_.__non_sym_perms_ = st.permissions();

    // If we failed to resolve the link, then only partially populate the
    // cache.
    if (!status_known(st)) {
      __data_.__cache_type_ = directory_entry::_RefreshSymlinkUnresolved;
      return error_code{};
    }
    // Otherwise, we resolved the link, potentially as not existing.
    // That's OK.
    __data_.__cache_type_ = directory_entry::_RefreshSymlink;
  }

  if (_VSTD_FS::is_regular_file(st))
    __data_.__size_ = static_cast<uintmax_t>(full_st.st_size);

  if (_VSTD_FS::exists(st)) {
    __data_.__nlink_ = static_cast<uintmax_t>(full_st.st_nlink);

    // Attempt to extract the mtime, and fail if it's not representable using
    // file_time_type. For now we ignore the error, as we'll report it when
    // the value is actually used.
    error_code ignored_ec;
    __data_.__write_time_ =
        __extract_last_write_time(__p_, full_st, &ignored_ec);
  }

  return failure_ec;
}
#else
error_code directory_entry::__do_refresh() noexcept {
  __data_.__reset();
  error_code failure_ec;

  file_status st = _VSTD_FS::symlink_status(__p_, failure_ec);
  if (!status_known(st)) {
    __data_.__reset();
    return failure_ec;
  }

  if (!_VSTD_FS::exists(st) || !_VSTD_FS::is_symlink(st)) {
    __data_.__cache_type_ = directory_entry::_RefreshNonSymlink;
    __data_.__type_ = st.type();
    __data_.__non_sym_perms_ = st.permissions();
  } else { // we have a symlink
    __data_.__sym_perms_ = st.permissions();
    // Get the information about the linked entity.
    // Ignore errors from stat, since we don't want errors regarding symlink
    // resolution to be reported to the user.
    error_code ignored_ec;
    st = _VSTD_FS::status(__p_, ignored_ec);

    __data_.__type_ = st.type();
    __data_.__non_sym_perms_ = st.permissions();

    // If we failed to resolve the link, then only partially populate the
    // cache.
    if (!status_known(st)) {
      __data_.__cache_type_ = directory_entry::_RefreshSymlinkUnresolved;
      return error_code{};
    }
    __data_.__cache_type_ = directory_entry::_RefreshSymlink;
  }

  // FIXME: This is currently broken, and the implementation only a placeholder.
  // We need to cache last_write_time, file_size, and hard_link_count here before
  // the implementation actually works.

  return failure_ec;
}
#endif

_LIBCPP_END_NAMESPACE_FILESYSTEM
