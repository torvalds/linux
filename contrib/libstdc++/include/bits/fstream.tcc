// File based streams -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

/** @file fstream.tcc
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 27.8  File-based streams
//

#ifndef _FSTREAM_TCC
#define _FSTREAM_TCC 1

#pragma GCC system_header

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<typename _CharT, typename _Traits>
    void
    basic_filebuf<_CharT, _Traits>::
    _M_allocate_internal_buffer()
    {
      // Allocate internal buffer only if one doesn't already exist
      // (either allocated or provided by the user via setbuf).
      if (!_M_buf_allocated && !_M_buf)
	{
	  _M_buf = new char_type[_M_buf_size];
	  _M_buf_allocated = true;
	}
    }

  template<typename _CharT, typename _Traits>
    void
    basic_filebuf<_CharT, _Traits>::
    _M_destroy_internal_buffer() throw()
    {
      if (_M_buf_allocated)
	{
	  delete [] _M_buf;
	  _M_buf = NULL;
	  _M_buf_allocated = false;
	}
      delete [] _M_ext_buf;
      _M_ext_buf = NULL;
      _M_ext_buf_size = 0;
      _M_ext_next = NULL;
      _M_ext_end = NULL;
    }

  template<typename _CharT, typename _Traits>
    basic_filebuf<_CharT, _Traits>::
    basic_filebuf() : __streambuf_type(), _M_lock(), _M_file(&_M_lock),
    _M_mode(ios_base::openmode(0)), _M_state_beg(), _M_state_cur(),
    _M_state_last(), _M_buf(NULL), _M_buf_size(BUFSIZ),
    _M_buf_allocated(false), _M_reading(false), _M_writing(false), _M_pback(), 
    _M_pback_cur_save(0), _M_pback_end_save(0), _M_pback_init(false),
    _M_codecvt(0), _M_ext_buf(0), _M_ext_buf_size(0), _M_ext_next(0),
    _M_ext_end(0)
    {
      if (has_facet<__codecvt_type>(this->_M_buf_locale))
	_M_codecvt = &use_facet<__codecvt_type>(this->_M_buf_locale);
    }

  template<typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::__filebuf_type*
    basic_filebuf<_CharT, _Traits>::
    open(const char* __s, ios_base::openmode __mode)
    {
      __filebuf_type *__ret = NULL;
      if (!this->is_open())
	{
	  _M_file.open(__s, __mode);
	  if (this->is_open())
	    {
	      _M_allocate_internal_buffer();
	      _M_mode = __mode;

	      // Setup initial buffer to 'uncommitted' mode.
	      _M_reading = false;
	      _M_writing = false;
	      _M_set_buffer(-1);

	      // Reset to initial state.
	      _M_state_last = _M_state_cur = _M_state_beg;

	      // 27.8.1.3,4
	      if ((__mode & ios_base::ate)
		  && this->seekoff(0, ios_base::end, __mode)
		  == pos_type(off_type(-1)))
		this->close();
	      else
		__ret = this;
	    }
	}
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::__filebuf_type*
    basic_filebuf<_CharT, _Traits>::
    close() throw()
    {
      __filebuf_type* __ret = NULL;
      if (this->is_open())
	{
	  bool __testfail = false;
	  try
	    {
	      if (!_M_terminate_output())
		__testfail = true;
	    }
	  catch(...)
	    { __testfail = true; }

	  // NB: Do this here so that re-opened filebufs will be cool...
	  _M_mode = ios_base::openmode(0);
	  _M_pback_init = false;
	  _M_destroy_internal_buffer();
	  _M_reading = false;
	  _M_writing = false;
	  _M_set_buffer(-1);
	  _M_state_last = _M_state_cur = _M_state_beg;

	  if (!_M_file.close())
	    __testfail = true;

	  if (!__testfail)
	    __ret = this;
	}
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    streamsize
    basic_filebuf<_CharT, _Traits>::
    showmanyc()
    {
      streamsize __ret = -1;
      const bool __testin = _M_mode & ios_base::in;
      if (__testin && this->is_open())
	{
	  // For a stateful encoding (-1) the pending sequence might be just
	  // shift and unshift prefixes with no actual character.
	  __ret = this->egptr() - this->gptr();

#if _GLIBCXX_HAVE_DOS_BASED_FILESYSTEM
	  // About this workaround, see libstdc++/20806.
	  const bool __testbinary = _M_mode & ios_base::binary;
	  if (__check_facet(_M_codecvt).encoding() >= 0
	      && __testbinary)
#else
	  if (__check_facet(_M_codecvt).encoding() >= 0)
#endif
	    __ret += _M_file.showmanyc() / _M_codecvt->max_length();
	}
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::int_type
    basic_filebuf<_CharT, _Traits>::
    underflow()
    {
      int_type __ret = traits_type::eof();
      const bool __testin = _M_mode & ios_base::in;
      if (__testin && !_M_writing)
	{
	  // Check for pback madness, and if so switch back to the
	  // normal buffers and jet outta here before expensive
	  // fileops happen...
	  _M_destroy_pback();

	  if (this->gptr() < this->egptr())
	    return traits_type::to_int_type(*this->gptr());

	  // Get and convert input sequence.
	  const size_t __buflen = _M_buf_size > 1 ? _M_buf_size - 1 : 1;

	  // Will be set to true if ::read() returns 0 indicating EOF.
	  bool __got_eof = false;
	  // Number of internal characters produced.
	  streamsize __ilen = 0;
	  codecvt_base::result __r = codecvt_base::ok;
	  if (__check_facet(_M_codecvt).always_noconv())
	    {
	      __ilen = _M_file.xsgetn(reinterpret_cast<char*>(this->eback()),
				      __buflen);
	      if (__ilen == 0)
		__got_eof = true;
	    }
	  else
	    {
              // Worst-case number of external bytes.
	      // XXX Not done encoding() == -1.
	      const int __enc = _M_codecvt->encoding();
	      streamsize __blen; // Minimum buffer size.
	      streamsize __rlen; // Number of chars to read.
	      if (__enc > 0)
		__blen = __rlen = __buflen * __enc;
	      else
		{
		  __blen = __buflen + _M_codecvt->max_length() - 1;
		  __rlen = __buflen;
		}
	      const streamsize __remainder = _M_ext_end - _M_ext_next;
	      __rlen = __rlen > __remainder ? __rlen - __remainder : 0;

	      // An imbue in 'read' mode implies first converting the external
	      // chars already present.
	      if (_M_reading && this->egptr() == this->eback() && __remainder)
		__rlen = 0;

	      // Allocate buffer if necessary and move unconverted
	      // bytes to front.
	      if (_M_ext_buf_size < __blen)
		{
		  char* __buf = new char[__blen];
		  if (__remainder)
		    std::memcpy(__buf, _M_ext_next, __remainder);

		  delete [] _M_ext_buf;
		  _M_ext_buf = __buf;
		  _M_ext_buf_size = __blen;
		}
	      else if (__remainder)
		std::memmove(_M_ext_buf, _M_ext_next, __remainder);

	      _M_ext_next = _M_ext_buf;
	      _M_ext_end = _M_ext_buf + __remainder;
	      _M_state_last = _M_state_cur;

	      do
		{
		  if (__rlen > 0)
		    {
		      // Sanity check!
		      // This may fail if the return value of
		      // codecvt::max_length() is bogus.
		      if (_M_ext_end - _M_ext_buf + __rlen > _M_ext_buf_size)
			{
			  __throw_ios_failure(__N("basic_filebuf::underflow "
					      "codecvt::max_length() "
					      "is not valid"));
			}
		      streamsize __elen = _M_file.xsgetn(_M_ext_end, __rlen);
		      if (__elen == 0)
			__got_eof = true;
		      else if (__elen == -1)
			break;
		      _M_ext_end += __elen;
		    }

		  char_type* __iend;
		  __r = _M_codecvt->in(_M_state_cur, _M_ext_next,
				       _M_ext_end, _M_ext_next, this->eback(),
				       this->eback() + __buflen, __iend);
		  if (__r == codecvt_base::noconv)
		    {
		      size_t __avail = _M_ext_end - _M_ext_buf;
		      __ilen = std::min(__avail, __buflen);
		      traits_type::copy(this->eback(),
					reinterpret_cast<char_type*>(_M_ext_buf), __ilen);
		      _M_ext_next = _M_ext_buf + __ilen;
		    }
		  else
		    __ilen = __iend - this->eback();

		  // _M_codecvt->in may return error while __ilen > 0: this is
		  // ok, and actually occurs in case of mixed encodings (e.g.,
		  // XML files).
		  if (__r == codecvt_base::error)
		    break;

		  __rlen = 1;
		}
	      while (__ilen == 0 && !__got_eof);
	    }

	  if (__ilen > 0)
	    {
	      _M_set_buffer(__ilen);
	      _M_reading = true;
	      __ret = traits_type::to_int_type(*this->gptr());
	    }
	  else if (__got_eof)
	    {
	      // If the actual end of file is reached, set 'uncommitted'
	      // mode, thus allowing an immediate write without an
	      // intervening seek.
	      _M_set_buffer(-1);
	      _M_reading = false;
	      // However, reaching it while looping on partial means that
	      // the file has got an incomplete character.
	      if (__r == codecvt_base::partial)
		__throw_ios_failure(__N("basic_filebuf::underflow "
				    "incomplete character in file"));
	    }
	  else if (__r == codecvt_base::error)
	    __throw_ios_failure(__N("basic_filebuf::underflow "
				"invalid byte sequence in file"));
	  else
	    __throw_ios_failure(__N("basic_filebuf::underflow "
				"error reading the file"));
	}
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::int_type
    basic_filebuf<_CharT, _Traits>::
    pbackfail(int_type __i)
    {
      int_type __ret = traits_type::eof();
      const bool __testin = _M_mode & ios_base::in;
      if (__testin && !_M_writing)
	{
	  // Remember whether the pback buffer is active, otherwise below
	  // we may try to store in it a second char (libstdc++/9761).
	  const bool __testpb = _M_pback_init;
	  const bool __testeof = traits_type::eq_int_type(__i, __ret);
	  int_type __tmp;
	  if (this->eback() < this->gptr())
	    {
	      this->gbump(-1);
	      __tmp = traits_type::to_int_type(*this->gptr());
	    }
	  else if (this->seekoff(-1, ios_base::cur) != pos_type(off_type(-1)))
	    {
	      __tmp = this->underflow();
	      if (traits_type::eq_int_type(__tmp, __ret))
		return __ret;
	    }
	  else
	    {
	      // At the beginning of the buffer, need to make a
	      // putback position available.  But the seek may fail
	      // (f.i., at the beginning of a file, see
	      // libstdc++/9439) and in that case we return
	      // traits_type::eof().
	      return __ret;
	    }

	  // Try to put back __i into input sequence in one of three ways.
	  // Order these tests done in is unspecified by the standard.
	  if (!__testeof && traits_type::eq_int_type(__i, __tmp))
	    __ret = __i;
	  else if (__testeof)
	    __ret = traits_type::not_eof(__i);
	  else if (!__testpb)
	    {
	      _M_create_pback();
	      _M_reading = true;
	      *this->gptr() = traits_type::to_char_type(__i);
	      __ret = __i;
	    }
	}
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::int_type
    basic_filebuf<_CharT, _Traits>::
    overflow(int_type __c)
    {
      int_type __ret = traits_type::eof();
      const bool __testeof = traits_type::eq_int_type(__c, __ret);
      const bool __testout = _M_mode & ios_base::out;
      if (__testout && !_M_reading)
	{
	  if (this->pbase() < this->pptr())
	    {
	      // If appropriate, append the overflow char.
	      if (!__testeof)
		{
		  *this->pptr() = traits_type::to_char_type(__c);
		  this->pbump(1);
		}

	      // Convert pending sequence to external representation,
	      // and output.
	      if (_M_convert_to_external(this->pbase(),
					 this->pptr() - this->pbase()))
		{
		  _M_set_buffer(0);
		  __ret = traits_type::not_eof(__c);
		}
	    }
	  else if (_M_buf_size > 1)
	    {
	      // Overflow in 'uncommitted' mode: set _M_writing, set
	      // the buffer to the initial 'write' mode, and put __c
	      // into the buffer.
	      _M_set_buffer(0);
	      _M_writing = true;
	      if (!__testeof)
		{
		  *this->pptr() = traits_type::to_char_type(__c);
		  this->pbump(1);
		}
	      __ret = traits_type::not_eof(__c);
	    }
	  else
	    {
	      // Unbuffered.
	      char_type __conv = traits_type::to_char_type(__c);
	      if (__testeof || _M_convert_to_external(&__conv, 1))
		{
		  _M_writing = true;
		  __ret = traits_type::not_eof(__c);
		}
	    }
	}
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    bool
    basic_filebuf<_CharT, _Traits>::
    _M_convert_to_external(_CharT* __ibuf, streamsize __ilen)
    {
      // Sizes of external and pending output.
      streamsize __elen;
      streamsize __plen;
      if (__check_facet(_M_codecvt).always_noconv())
	{
	  __elen = _M_file.xsputn(reinterpret_cast<char*>(__ibuf), __ilen);
	  __plen = __ilen;
	}
      else
	{
	  // Worst-case number of external bytes needed.
	  // XXX Not done encoding() == -1.
	  streamsize __blen = __ilen * _M_codecvt->max_length();
	  char* __buf = static_cast<char*>(__builtin_alloca(__blen));

	  char* __bend;
	  const char_type* __iend;
	  codecvt_base::result __r;
	  __r = _M_codecvt->out(_M_state_cur, __ibuf, __ibuf + __ilen,
				__iend, __buf, __buf + __blen, __bend);

	  if (__r == codecvt_base::ok || __r == codecvt_base::partial)
	    __blen = __bend - __buf;
	  else if (__r == codecvt_base::noconv)
	    {
	      // Same as the always_noconv case above.
	      __buf = reinterpret_cast<char*>(__ibuf);
	      __blen = __ilen;
	    }
	  else
	    __throw_ios_failure(__N("basic_filebuf::_M_convert_to_external "
				    "conversion error"));
  
	  __elen = _M_file.xsputn(__buf, __blen);
	  __plen = __blen;

	  // Try once more for partial conversions.
	  if (__r == codecvt_base::partial && __elen == __plen)
	    {
	      const char_type* __iresume = __iend;
	      streamsize __rlen = this->pptr() - __iend;
	      __r = _M_codecvt->out(_M_state_cur, __iresume,
				    __iresume + __rlen, __iend, __buf,
				    __buf + __blen, __bend);
	      if (__r != codecvt_base::error)
		{
		  __rlen = __bend - __buf;
		  __elen = _M_file.xsputn(__buf, __rlen);
		  __plen = __rlen;
		}
	      else
		__throw_ios_failure(__N("basic_filebuf::_M_convert_to_external "
					"conversion error"));
	    }
	}
      return __elen == __plen;
    }

   template<typename _CharT, typename _Traits>
     streamsize
     basic_filebuf<_CharT, _Traits>::
     xsgetn(_CharT* __s, streamsize __n)
     {
       // Clear out pback buffer before going on to the real deal...
       streamsize __ret = 0;
       if (_M_pback_init)
	 {
	   if (__n > 0 && this->gptr() == this->eback())
	     {
	       *__s++ = *this->gptr();
	       this->gbump(1);
	       __ret = 1;
	       --__n;
	     }
	   _M_destroy_pback();
	 }
       
       // Optimization in the always_noconv() case, to be generalized in the
       // future: when __n > __buflen we read directly instead of using the
       // buffer repeatedly.
       const bool __testin = _M_mode & ios_base::in;
       const streamsize __buflen = _M_buf_size > 1 ? _M_buf_size - 1 : 1;

       if (__n > __buflen && __check_facet(_M_codecvt).always_noconv()
	   && __testin && !_M_writing)
	 {
	   // First, copy the chars already present in the buffer.
	   const streamsize __avail = this->egptr() - this->gptr();
	   if (__avail != 0)
	     {
	       if (__avail == 1)
		 *__s = *this->gptr();
	       else
		 traits_type::copy(__s, this->gptr(), __avail);
	       __s += __avail;
	       this->gbump(__avail);
	       __ret += __avail;
	       __n -= __avail;
	     }

	   // Need to loop in case of short reads (relatively common
	   // with pipes).
	   streamsize __len;
	   for (;;)
	     {
	       __len = _M_file.xsgetn(reinterpret_cast<char*>(__s),
				      __n);
	       if (__len == -1)
		 __throw_ios_failure(__N("basic_filebuf::xsgetn "
					 "error reading the file"));
	       if (__len == 0)
		 break;

	       __n -= __len;
	       __ret += __len;
	       if (__n == 0)
		 break;

	       __s += __len;
	     }

	   if (__n == 0)
	     {
	       _M_set_buffer(0);
	       _M_reading = true;
	     }
	   else if (__len == 0)
	     {
	       // If end of file is reached, set 'uncommitted'
	       // mode, thus allowing an immediate write without
	       // an intervening seek.
	       _M_set_buffer(-1);
	       _M_reading = false;
	     }
	 }
       else
	 __ret += __streambuf_type::xsgetn(__s, __n);

       return __ret;
     }

   template<typename _CharT, typename _Traits>
     streamsize
     basic_filebuf<_CharT, _Traits>::
     xsputn(const _CharT* __s, streamsize __n)
     {
       // Optimization in the always_noconv() case, to be generalized in the
       // future: when __n is sufficiently large we write directly instead of
       // using the buffer.
       streamsize __ret = 0;
       const bool __testout = _M_mode & ios_base::out;
       if (__check_facet(_M_codecvt).always_noconv()
	   && __testout && !_M_reading)
	{
	  // Measurement would reveal the best choice.
	  const streamsize __chunk = 1ul << 10;
	  streamsize __bufavail = this->epptr() - this->pptr();

	  // Don't mistake 'uncommitted' mode buffered with unbuffered.
	  if (!_M_writing && _M_buf_size > 1)
	    __bufavail = _M_buf_size - 1;

	  const streamsize __limit = std::min(__chunk, __bufavail);
	  if (__n >= __limit)
	    {
	      const streamsize __buffill = this->pptr() - this->pbase();
	      const char* __buf = reinterpret_cast<const char*>(this->pbase());
	      __ret = _M_file.xsputn_2(__buf, __buffill,
				       reinterpret_cast<const char*>(__s),
				       __n);
	      if (__ret == __buffill + __n)
		{
		  _M_set_buffer(0);
		  _M_writing = true;
		}
	      if (__ret > __buffill)
		__ret -= __buffill;
	      else
		__ret = 0;
	    }
	  else
	    __ret = __streambuf_type::xsputn(__s, __n);
	}
       else
	 __ret = __streambuf_type::xsputn(__s, __n);
       return __ret;
    }

  template<typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::__streambuf_type*
    basic_filebuf<_CharT, _Traits>::
    setbuf(char_type* __s, streamsize __n)
    {
      if (!this->is_open())
	{
	  if (__s == 0 && __n == 0)
	    _M_buf_size = 1;
	  else if (__s && __n > 0)
	    {
	      // This is implementation-defined behavior, and assumes that
	      // an external char_type array of length __n exists and has
	      // been pre-allocated. If this is not the case, things will
	      // quickly blow up. When __n > 1, __n - 1 positions will be
	      // used for the get area, __n - 1 for the put area and 1
	      // position to host the overflow char of a full put area.
	      // When __n == 1, 1 position will be used for the get area
	      // and 0 for the put area, as in the unbuffered case above.
	      _M_buf = __s;
	      _M_buf_size = __n;
	    }
	}
      return this;
    }


  // According to 27.8.1.4 p11 - 13, seekoff should ignore the last
  // argument (of type openmode).
  template<typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::pos_type
    basic_filebuf<_CharT, _Traits>::
    seekoff(off_type __off, ios_base::seekdir __way, ios_base::openmode)
    {
      int __width = 0;
      if (_M_codecvt)
	__width = _M_codecvt->encoding();
      if (__width < 0)
	__width = 0;

      pos_type __ret =  pos_type(off_type(-1));
      const bool __testfail = __off != 0 && __width <= 0;
      if (this->is_open() && !__testfail)
	{
	  // Ditch any pback buffers to avoid confusion.
	  _M_destroy_pback();

	  // Correct state at destination. Note that this is the correct
	  // state for the current position during output, because
	  // codecvt::unshift() returns the state to the initial state.
	  // This is also the correct state at the end of the file because
	  // an unshift sequence should have been written at the end.
	  __state_type __state = _M_state_beg;
	  off_type __computed_off = __off * __width;
	  if (_M_reading && __way == ios_base::cur)
	    {
	      if (_M_codecvt->always_noconv())
		__computed_off += this->gptr() - this->egptr();
	      else
		{
		  // Calculate offset from _M_ext_buf that corresponds
		  // to gptr(). Note: uses _M_state_last, which
		  // corresponds to eback().
		  const int __gptr_off =
		    _M_codecvt->length(_M_state_last, _M_ext_buf, _M_ext_next,
				       this->gptr() - this->eback());
		  __computed_off += _M_ext_buf + __gptr_off - _M_ext_end;

		  // _M_state_last is modified by codecvt::length() so
		  // it now corresponds to gptr().
		  __state = _M_state_last;
		}
	    }
	  __ret = _M_seek(__computed_off, __way, __state);
	}
      return __ret;
    }

  // _GLIBCXX_RESOLVE_LIB_DEFECTS
  // 171. Strange seekpos() semantics due to joint position
  // According to the resolution of DR 171, seekpos should ignore the last
  // argument (of type openmode).
  template<typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::pos_type
    basic_filebuf<_CharT, _Traits>::
    seekpos(pos_type __pos, ios_base::openmode)
    {
      pos_type __ret =  pos_type(off_type(-1));
      if (this->is_open())
	{
	  // Ditch any pback buffers to avoid confusion.
	  _M_destroy_pback();
	  __ret = _M_seek(off_type(__pos), ios_base::beg, __pos.state());
	}
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    typename basic_filebuf<_CharT, _Traits>::pos_type
    basic_filebuf<_CharT, _Traits>::
    _M_seek(off_type __off, ios_base::seekdir __way, __state_type __state)
    {
      pos_type __ret = pos_type(off_type(-1));
      if (_M_terminate_output())
	{
	  // Returns pos_type(off_type(-1)) in case of failure.
	  __ret = pos_type(_M_file.seekoff(__off, __way));
	  if (__ret != pos_type(off_type(-1)))
	    {
	      _M_reading = false;
	      _M_writing = false;
	      _M_ext_next = _M_ext_end = _M_ext_buf;
	      _M_set_buffer(-1);
	      _M_state_cur = __state;
	      __ret.state(_M_state_cur);
	    }
	}
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    bool
    basic_filebuf<_CharT, _Traits>::
    _M_terminate_output()
    {
      // Part one: update the output sequence.
      bool __testvalid = true;
      if (this->pbase() < this->pptr())
	{
	  const int_type __tmp = this->overflow();
	  if (traits_type::eq_int_type(__tmp, traits_type::eof()))
	    __testvalid = false;
	}

      // Part two: output unshift sequence.
      if (_M_writing && !__check_facet(_M_codecvt).always_noconv()
	  && __testvalid)
	{
	  // Note: this value is arbitrary, since there is no way to
	  // get the length of the unshift sequence from codecvt,
	  // without calling unshift.
	  const size_t __blen = 128;
	  char __buf[__blen];
	  codecvt_base::result __r;
	  streamsize __ilen = 0;

	  do
	    {
	      char* __next;
	      __r = _M_codecvt->unshift(_M_state_cur, __buf,
					__buf + __blen, __next);
	      if (__r == codecvt_base::error)
		__testvalid = false;
	      else if (__r == codecvt_base::ok ||
		       __r == codecvt_base::partial)
		{
		  __ilen = __next - __buf;
		  if (__ilen > 0)
		    {
		      const streamsize __elen = _M_file.xsputn(__buf, __ilen);
		      if (__elen != __ilen)
			__testvalid = false;
		    }
		}
	    }
	  while (__r == codecvt_base::partial && __ilen > 0 && __testvalid);

	  if (__testvalid)
	    {
	      // This second call to overflow() is required by the standard,
	      // but it's not clear why it's needed, since the output buffer
	      // should be empty by this point (it should have been emptied
	      // in the first call to overflow()).
	      const int_type __tmp = this->overflow();
	      if (traits_type::eq_int_type(__tmp, traits_type::eof()))
		__testvalid = false;
	    }
	}
      return __testvalid;
    }

  template<typename _CharT, typename _Traits>
    int
    basic_filebuf<_CharT, _Traits>::
    sync()
    {
      // Make sure that the internal buffer resyncs its idea of
      // the file position with the external file.
      int __ret = 0;
      if (this->pbase() < this->pptr())
	{
	  const int_type __tmp = this->overflow();
	  if (traits_type::eq_int_type(__tmp, traits_type::eof()))
	    __ret = -1;
	}
      return __ret;
    }

  template<typename _CharT, typename _Traits>
    void
    basic_filebuf<_CharT, _Traits>::
    imbue(const locale& __loc)
    {
      bool __testvalid = true;

      const __codecvt_type* _M_codecvt_tmp = 0;
      if (__builtin_expect(has_facet<__codecvt_type>(__loc), true))
	_M_codecvt_tmp = &use_facet<__codecvt_type>(__loc);

      if (this->is_open())
	{
	  // encoding() == -1 is ok only at the beginning.
	  if ((_M_reading || _M_writing)
	      && __check_facet(_M_codecvt).encoding() == -1)
	    __testvalid = false;
	  else
	    {
	      if (_M_reading)
		{
		  if (__check_facet(_M_codecvt).always_noconv())
		    {
		      if (_M_codecvt_tmp
			  && !__check_facet(_M_codecvt_tmp).always_noconv())
			__testvalid = this->seekoff(0, ios_base::cur, _M_mode)
			              != pos_type(off_type(-1));
		    }
		  else
		    {
		      // External position corresponding to gptr().
		      _M_ext_next = _M_ext_buf
			+ _M_codecvt->length(_M_state_last, _M_ext_buf, _M_ext_next,
					     this->gptr() - this->eback());
		      const streamsize __remainder = _M_ext_end - _M_ext_next;
		      if (__remainder)
			std::memmove(_M_ext_buf, _M_ext_next, __remainder);

		      _M_ext_next = _M_ext_buf;
		      _M_ext_end = _M_ext_buf + __remainder;
		      _M_set_buffer(-1);
		      _M_state_last = _M_state_cur = _M_state_beg;
		    }
		}
	      else if (_M_writing && (__testvalid = _M_terminate_output()))
		_M_set_buffer(-1);
	    }
	}

      if (__testvalid)
	_M_codecvt = _M_codecvt_tmp;
      else
	_M_codecvt = 0;
    }

  // Inhibit implicit instantiations for required instantiations,
  // which are defined via explicit instantiations elsewhere.
  // NB:  This syntax is a GNU extension.
#if _GLIBCXX_EXTERN_TEMPLATE
  extern template class basic_filebuf<char>;
  extern template class basic_ifstream<char>;
  extern template class basic_ofstream<char>;
  extern template class basic_fstream<char>;

#ifdef _GLIBCXX_USE_WCHAR_T
  extern template class basic_filebuf<wchar_t>;
  extern template class basic_ifstream<wchar_t>;
  extern template class basic_ofstream<wchar_t>;
  extern template class basic_fstream<wchar_t>;
#endif
#endif

_GLIBCXX_END_NAMESPACE

#endif
