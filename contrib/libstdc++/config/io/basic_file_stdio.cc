// Wrapper of C-language FILE struct -*- C++ -*-

// Copyright (C) 2000, 2001, 2002, 2003, 2004, 2006
// Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

//
// ISO C++ 14882: 27.8  File-based streams
//

#include <bits/basic_file.h>
#include <fcntl.h>
#include <errno.h>

#ifdef _GLIBCXX_HAVE_POLL
#include <poll.h>
#endif

// Pick up ioctl on Solaris 2.8
#ifdef _GLIBCXX_HAVE_UNISTD_H
#include <unistd.h>
#endif

// Pick up FIONREAD on Solaris 2
#ifdef _GLIBCXX_HAVE_SYS_IOCTL_H
#define BSD_COMP 
#include <sys/ioctl.h>
#endif

// Pick up FIONREAD on Solaris 2.5.
#ifdef _GLIBCXX_HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#ifdef _GLIBCXX_HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#if defined(_GLIBCXX_HAVE_S_ISREG) || defined(_GLIBCXX_HAVE_S_IFREG)
# include <sys/stat.h>
# ifdef _GLIBCXX_HAVE_S_ISREG
#  define _GLIBCXX_ISREG(x) S_ISREG(x)
# else
#  define _GLIBCXX_ISREG(x) (((x) & S_IFMT) == S_IFREG)
# endif
#endif

#include <limits> // For <off_t>::max() and min() and <streamsize>::max()

namespace 
{
  // Map ios_base::openmode flags to a string for use in fopen().
  // Table of valid combinations as given in [lib.filebuf.members]/2.
  static const char*
  fopen_mode(std::ios_base::openmode mode)
  {
    enum 
      {
	in     = std::ios_base::in,
	out    = std::ios_base::out,
	trunc  = std::ios_base::trunc,
	app    = std::ios_base::app,
	binary = std::ios_base::binary
      };
    
    switch (mode & (in|out|trunc|app|binary))
      {
      case (   out                 ): return "w";  
      case (   out      |app       ): return "a";  
      case (   out|trunc           ): return "w";  
      case (in                     ): return "r";  
      case (in|out                 ): return "r+"; 
      case (in|out|trunc           ): return "w+"; 
      // Extension to Table 92.
      case (in|out      |app       ): return "a+"; 
	
      case (   out          |binary): return "wb"; 
      case (   out      |app|binary): return "ab"; 
      case (   out|trunc    |binary): return "wb"; 
      case (in              |binary): return "rb"; 
      case (in|out          |binary): return "r+b";
      case (in|out|trunc    |binary): return "w+b";
      // Extension to Table 92.
      case (in|out      |app|binary): return "a+b";
	
      default: return 0; // invalid
      }
  }

  // Wrapper handling partial write.
  static std::streamsize
  xwrite(int __fd, const char* __s, std::streamsize __n)
  {
    std::streamsize __nleft = __n;

    for (;;)
      {
	const std::streamsize __ret = write(__fd, __s, __nleft);
	if (__ret == -1L && errno == EINTR)
	  continue;
	if (__ret == -1L)
	  break;

	__nleft -= __ret;
	if (__nleft == 0)
	  break;

	__s += __ret;
      }

    return __n - __nleft;
  }

#ifdef _GLIBCXX_HAVE_WRITEV
  // Wrapper handling partial writev.
  static std::streamsize
  xwritev(int __fd, const char* __s1, std::streamsize __n1,
	  const char* __s2, std::streamsize __n2)
  {
    std::streamsize __nleft = __n1 + __n2;
    std::streamsize __n1_left = __n1;

    struct iovec __iov[2];
    __iov[1].iov_base = const_cast<char*>(__s2);
    __iov[1].iov_len = __n2;

    for (;;)
      {
	__iov[0].iov_base = const_cast<char*>(__s1);
	__iov[0].iov_len = __n1_left;

	const std::streamsize __ret = writev(__fd, __iov, 2);
	if (__ret == -1L && errno == EINTR)
	  continue;
	if (__ret == -1L)
	  break;

	__nleft -= __ret;
	if (__nleft == 0)
	  break;

	const std::streamsize __off = __ret - __n1_left;
	if (__off >= 0)
	  {
	    __nleft -= xwrite(__fd, __s2 + __off, __n2 - __off);
	    break;
	  }
	
	__s1 += __ret;
	__n1_left -= __ret;
      }

    return __n1 + __n2 - __nleft;
  }
#endif
} // anonymous namespace


_GLIBCXX_BEGIN_NAMESPACE(std)

  // Definitions for __basic_file<char>.
  __basic_file<char>::__basic_file(__c_lock* /*__lock*/) 
  : _M_cfile(NULL), _M_cfile_created(false) { }

  __basic_file<char>::~__basic_file()
  { this->close(); }
      
  __basic_file<char>*
  __basic_file<char>::sys_open(__c_file* __file, ios_base::openmode) 
  {
    __basic_file* __ret = NULL;
    if (!this->is_open() && __file)
      {
	int __err;
	errno = 0;	
	do
	  __err = this->sync();
	while (__err && errno == EINTR);
	if (!__err)
	  {
	    _M_cfile = __file;
	    _M_cfile_created = false;
	    __ret = this;
	  }
      }
    return __ret;
  }
  
  __basic_file<char>*
  __basic_file<char>::sys_open(int __fd, ios_base::openmode __mode)
  {
    __basic_file* __ret = NULL;
    const char* __c_mode = fopen_mode(__mode);
    if (__c_mode && !this->is_open() && (_M_cfile = fdopen(__fd, __c_mode)))
      {
	char* __buf = NULL;
	_M_cfile_created = true;
	if (__fd == 0)
	  setvbuf(_M_cfile, __buf, _IONBF, 0);
	__ret = this;
      }
    return __ret;
  }
  
  __basic_file<char>* 
  __basic_file<char>::open(const char* __name, ios_base::openmode __mode, 
			   int /*__prot*/)
  {
    __basic_file* __ret = NULL;
    const char* __c_mode = fopen_mode(__mode);
    if (__c_mode && !this->is_open())
      {
#ifdef _GLIBCXX_USE_LFS
	if ((_M_cfile = fopen64(__name, __c_mode)))
#else
	if ((_M_cfile = fopen(__name, __c_mode)))
#endif
	  {
	    _M_cfile_created = true;
	    __ret = this;
	  }
      }
    return __ret;
  }
  
  bool 
  __basic_file<char>::is_open() const 
  { return _M_cfile != 0; }
  
  int 
  __basic_file<char>::fd() 
  { return fileno(_M_cfile); }
  
  __c_file*
  __basic_file<char>::file() 
  { return _M_cfile; }
  
  __basic_file<char>* 
  __basic_file<char>::close()
  { 
    __basic_file* __ret = static_cast<__basic_file*>(NULL);
    if (this->is_open())
      {
	int __err = 0;
	if (_M_cfile_created)
	  {
	    // In general, no need to zero errno in advance if checking
	    // for error first. However, C89/C99 (at variance with IEEE
	    // 1003.1, f.i.) do not mandate that fclose must set errno
	    // upon error.
	    errno = 0;
	    do
	      __err = fclose(_M_cfile);
	    while (__err && errno == EINTR);
	  }
	_M_cfile = 0;
	if (!__err)
	  __ret = this;
      }
    return __ret;
  }
 
  streamsize 
  __basic_file<char>::xsgetn(char* __s, streamsize __n)
  {
    streamsize __ret;
    do
      __ret = read(this->fd(), __s, __n);
    while (__ret == -1L && errno == EINTR);
    return __ret;
  }

  streamsize 
  __basic_file<char>::xsputn(const char* __s, streamsize __n)
  { return xwrite(this->fd(), __s, __n); }

  streamsize 
  __basic_file<char>::xsputn_2(const char* __s1, streamsize __n1,
			       const char* __s2, streamsize __n2)
  {
    streamsize __ret = 0;
#ifdef _GLIBCXX_HAVE_WRITEV
    __ret = xwritev(this->fd(), __s1, __n1, __s2, __n2);
#else
    if (__n1)
      __ret = xwrite(this->fd(), __s1, __n1);

    if (__ret == __n1)
      __ret += xwrite(this->fd(), __s2, __n2);
#endif
    return __ret;
  }

  streamoff
  __basic_file<char>::seekoff(streamoff __off, ios_base::seekdir __way)
  {
#ifdef _GLIBCXX_USE_LFS
    return lseek64(this->fd(), __off, __way);
#else
    if (__off > numeric_limits<off_t>::max()
	|| __off < numeric_limits<off_t>::min())
      return -1L;
    return lseek(this->fd(), __off, __way);
#endif
  }

  int 
  __basic_file<char>::sync() 
  { return fflush(_M_cfile); }

  streamsize
  __basic_file<char>::showmanyc()
  {
#ifdef FIONREAD
    // Pipes and sockets.    
#ifdef _GLIBCXX_FIONREAD_TAKES_OFF_T
    off_t __num = 0;
#else
    int __num = 0;
#endif
    int __r = ioctl(this->fd(), FIONREAD, &__num);
    if (!__r && __num >= 0)
      return __num; 
#endif    

#ifdef _GLIBCXX_HAVE_POLL
    // Cheap test.
    struct pollfd __pfd[1];
    __pfd[0].fd = this->fd();
    __pfd[0].events = POLLIN;
    if (poll(__pfd, 1, 0) <= 0)
      return 0;
#endif   

#if defined(_GLIBCXX_HAVE_S_ISREG) || defined(_GLIBCXX_HAVE_S_IFREG)
    // Regular files.
#ifdef _GLIBCXX_USE_LFS
    struct stat64 __buffer;
    const int __err = fstat64(this->fd(), &__buffer);
    if (!__err && _GLIBCXX_ISREG(__buffer.st_mode))
      {
	const streamoff __off = __buffer.st_size - lseek64(this->fd(), 0,
							   ios_base::cur);
	return std::min(__off, streamoff(numeric_limits<streamsize>::max()));
      }
#else
    struct stat __buffer;
    const int __err = fstat(this->fd(), &__buffer);
    if (!__err && _GLIBCXX_ISREG(__buffer.st_mode))
      return __buffer.st_size - lseek(this->fd(), 0, ios_base::cur);
#endif
#endif
    return 0;
  }

_GLIBCXX_END_NAMESPACE

