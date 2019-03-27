// Iostreams base classes -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2001, 2002, 2003, 2004, 2005
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

/** @file basic_ios.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _BASIC_IOS_H
#define _BASIC_IOS_H 1

#pragma GCC system_header

#include <bits/streambuf_iterator.h>
#include <bits/localefwd.h>
#include <bits/locale_classes.h>
#include <bits/locale_facets.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // 27.4.5  Template class basic_ios
  /**
   *  @brief  Virtual base class for all stream classes.
   *
   *  Most of the member functions called dispatched on stream objects
   *  (e.g., @c std::cout.foo(bar);) are consolidated in this class.
  */
  template<typename _CharT, typename _Traits>
    class basic_ios : public ios_base
    {
    public:
      //@{
      /**
       *  These are standard types.  They permit a standardized way of
       *  referring to names of (or names dependant on) the template
       *  parameters, which are specific to the implementation.
      */
      typedef _CharT                                 char_type;
      typedef typename _Traits::int_type             int_type;
      typedef typename _Traits::pos_type             pos_type;
      typedef typename _Traits::off_type             off_type;
      typedef _Traits                                traits_type;
      //@}

      //@{
      /**
       *  @if maint
       *  These are non-standard types.
       *  @endif
      */
      typedef ctype<_CharT>                          __ctype_type;
      typedef num_put<_CharT, ostreambuf_iterator<_CharT, _Traits> >
						     __num_put_type;
      typedef num_get<_CharT, istreambuf_iterator<_CharT, _Traits> >
						     __num_get_type;
      //@}

      // Data members:
    protected:
      basic_ostream<_CharT, _Traits>*                _M_tie;
      mutable char_type                              _M_fill;
      mutable bool                                   _M_fill_init;
      basic_streambuf<_CharT, _Traits>*              _M_streambuf;

      // Cached use_facet<ctype>, which is based on the current locale info.
      const __ctype_type*                            _M_ctype;
      // For ostream.
      const __num_put_type*                          _M_num_put;
      // For istream.
      const __num_get_type*                          _M_num_get;

    public:
      //@{
      /**
       *  @brief  The quick-and-easy status check.
       *
       *  This allows you to write constructs such as
       *  "if (!a_stream) ..." and "while (a_stream) ..."
      */
      operator void*() const
      { return this->fail() ? 0 : const_cast<basic_ios*>(this); }

      bool
      operator!() const
      { return this->fail(); }
      //@}

      /**
       *  @brief  Returns the error state of the stream buffer.
       *  @return  A bit pattern (well, isn't everything?)
       *
       *  See std::ios_base::iostate for the possible bit values.  Most
       *  users will call one of the interpreting wrappers, e.g., good().
      */
      iostate
      rdstate() const
      { return _M_streambuf_state; }

      /**
       *  @brief  [Re]sets the error state.
       *  @param  state  The new state flag(s) to set.
       *
       *  See std::ios_base::iostate for the possible bit values.  Most
       *  users will not need to pass an argument.
      */
      void
      clear(iostate __state = goodbit);

      /**
       *  @brief  Sets additional flags in the error state.
       *  @param  state  The additional state flag(s) to set.
       *
       *  See std::ios_base::iostate for the possible bit values.
      */
      void
      setstate(iostate __state)
      { this->clear(this->rdstate() | __state); }

      // Flip the internal state on for the proper state bits, then re
      // throws the propagated exception if bit also set in
      // exceptions().
      void
      _M_setstate(iostate __state)
      {
	// 27.6.1.2.1 Common requirements.
	// Turn this on without causing an ios::failure to be thrown.
	_M_streambuf_state |= __state;
	if (this->exceptions() & __state)
	  __throw_exception_again;
      }

      /**
       *  @brief  Fast error checking.
       *  @return  True if no error flags are set.
       *
       *  A wrapper around rdstate.
      */
      bool
      good() const
      { return this->rdstate() == 0; }

      /**
       *  @brief  Fast error checking.
       *  @return  True if the eofbit is set.
       *
       *  Note that other iostate flags may also be set.
      */
      bool
      eof() const
      { return (this->rdstate() & eofbit) != 0; }

      /**
       *  @brief  Fast error checking.
       *  @return  True if either the badbit or the failbit is set.
       *
       *  Checking the badbit in fail() is historical practice.
       *  Note that other iostate flags may also be set.
      */
      bool
      fail() const
      { return (this->rdstate() & (badbit | failbit)) != 0; }

      /**
       *  @brief  Fast error checking.
       *  @return  True if the badbit is set.
       *
       *  Note that other iostate flags may also be set.
      */
      bool
      bad() const
      { return (this->rdstate() & badbit) != 0; }

      /**
       *  @brief  Throwing exceptions on errors.
       *  @return  The current exceptions mask.
       *
       *  This changes nothing in the stream.  See the one-argument version
       *  of exceptions(iostate) for the meaning of the return value.
      */
      iostate
      exceptions() const
      { return _M_exception; }

      /**
       *  @brief  Throwing exceptions on errors.
       *  @param  except  The new exceptions mask.
       *
       *  By default, error flags are set silently.  You can set an
       *  exceptions mask for each stream; if a bit in the mask becomes set
       *  in the error flags, then an exception of type
       *  std::ios_base::failure is thrown.
       *
       *  If the error flage is already set when the exceptions mask is
       *  added, the exception is immediately thrown.  Try running the
       *  following under GCC 3.1 or later:
       *  @code
       *  #include <iostream>
       *  #include <fstream>
       *  #include <exception>
       *
       *  int main()
       *  {
       *      std::set_terminate (__gnu_cxx::__verbose_terminate_handler);
       *
       *      std::ifstream f ("/etc/motd");
       *
       *      std::cerr << "Setting badbit\n";
       *      f.setstate (std::ios_base::badbit);
       *
       *      std::cerr << "Setting exception mask\n";
       *      f.exceptions (std::ios_base::badbit);
       *  }
       *  @endcode
      */
      void
      exceptions(iostate __except)
      {
        _M_exception = __except;
        this->clear(_M_streambuf_state);
      }

      // Constructor/destructor:
      /**
       *  @brief  Constructor performs initialization.
       *
       *  The parameter is passed by derived streams.
      */
      explicit
      basic_ios(basic_streambuf<_CharT, _Traits>* __sb)
      : ios_base(), _M_tie(0), _M_fill(), _M_fill_init(false), _M_streambuf(0),
      _M_ctype(0), _M_num_put(0), _M_num_get(0)
      { this->init(__sb); }

      /**
       *  @brief  Empty.
       *
       *  The destructor does nothing.  More specifically, it does not
       *  destroy the streambuf held by rdbuf().
      */
      virtual
      ~basic_ios() { }

      // Members:
      /**
       *  @brief  Fetches the current @e tied stream.
       *  @return  A pointer to the tied stream, or NULL if the stream is
       *           not tied.
       *
       *  A stream may be @e tied (or synchronized) to a second output
       *  stream.  When this stream performs any I/O, the tied stream is
       *  first flushed.  For example, @c std::cin is tied to @c std::cout.
      */
      basic_ostream<_CharT, _Traits>*
      tie() const
      { return _M_tie; }

      /**
       *  @brief  Ties this stream to an output stream.
       *  @param  tiestr  The output stream.
       *  @return  The previously tied output stream, or NULL if the stream
       *           was not tied.
       *
       *  This sets up a new tie; see tie() for more.
      */
      basic_ostream<_CharT, _Traits>*
      tie(basic_ostream<_CharT, _Traits>* __tiestr)
      {
        basic_ostream<_CharT, _Traits>* __old = _M_tie;
        _M_tie = __tiestr;
        return __old;
      }

      /**
       *  @brief  Accessing the underlying buffer.
       *  @return  The current stream buffer.
       *
       *  This does not change the state of the stream.
      */
      basic_streambuf<_CharT, _Traits>*
      rdbuf() const
      { return _M_streambuf; }

      /**
       *  @brief  Changing the underlying buffer.
       *  @param  sb  The new stream buffer.
       *  @return  The previous stream buffer.
       *
       *  Associates a new buffer with the current stream, and clears the
       *  error state.
       *
       *  Due to historical accidents which the LWG refuses to correct, the
       *  I/O library suffers from a design error:  this function is hidden
       *  in derived classes by overrides of the zero-argument @c rdbuf(),
       *  which is non-virtual for hysterical raisins.  As a result, you
       *  must use explicit qualifications to access this function via any
       *  derived class.  For example:
       *
       *  @code
       *  std::fstream     foo;         // or some other derived type
       *  std::streambuf*  p = .....;
       *
       *  foo.ios::rdbuf(p);            // ios == basic_ios<char>
       *  @endcode
      */
      basic_streambuf<_CharT, _Traits>*
      rdbuf(basic_streambuf<_CharT, _Traits>* __sb);

      /**
       *  @brief  Copies fields of __rhs into this.
       *  @param  __rhs  The source values for the copies.
       *  @return  Reference to this object.
       *
       *  All fields of __rhs are copied into this object except that rdbuf()
       *  and rdstate() remain unchanged.  All values in the pword and iword
       *  arrays are copied.  Before copying, each callback is invoked with
       *  erase_event.  After copying, each (new) callback is invoked with
       *  copyfmt_event.  The final step is to copy exceptions().
      */
      basic_ios&
      copyfmt(const basic_ios& __rhs);

      /**
       *  @brief  Retreives the "empty" character.
       *  @return  The current fill character.
       *
       *  It defaults to a space (' ') in the current locale.
      */
      char_type
      fill() const
      {
	if (!_M_fill_init)
	  {
	    _M_fill = this->widen(' ');
	    _M_fill_init = true;
	  }
	return _M_fill;
      }

      /**
       *  @brief  Sets a new "empty" character.
       *  @param  ch  The new character.
       *  @return  The previous fill character.
       *
       *  The fill character is used to fill out space when P+ characters
       *  have been requested (e.g., via setw), Q characters are actually
       *  used, and Q<P.  It defaults to a space (' ') in the current locale.
      */
      char_type
      fill(char_type __ch)
      {
	char_type __old = this->fill();
	_M_fill = __ch;
	return __old;
      }

      // Locales:
      /**
       *  @brief  Moves to a new locale.
       *  @param  loc  The new locale.
       *  @return  The previous locale.
       *
       *  Calls @c ios_base::imbue(loc), and if a stream buffer is associated
       *  with this stream, calls that buffer's @c pubimbue(loc).
       *
       *  Additional l10n notes are at
       *  http://gcc.gnu.org/onlinedocs/libstdc++/22_locale/howto.html
      */
      locale
      imbue(const locale& __loc);

      /**
       *  @brief  Squeezes characters.
       *  @param  c  The character to narrow.
       *  @param  dfault  The character to narrow.
       *  @return  The narrowed character.
       *
       *  Maps a character of @c char_type to a character of @c char,
       *  if possible.
       *
       *  Returns the result of
       *  @code
       *    std::use_facet<ctype<char_type> >(getloc()).narrow(c,dfault)
       *  @endcode
       *
       *  Additional l10n notes are at
       *  http://gcc.gnu.org/onlinedocs/libstdc++/22_locale/howto.html
      */
      char
      narrow(char_type __c, char __dfault) const;

      /**
       *  @brief  Widens characters.
       *  @param  c  The character to widen.
       *  @return  The widened character.
       *
       *  Maps a character of @c char to a character of @c char_type.
       *
       *  Returns the result of
       *  @code
       *    std::use_facet<ctype<char_type> >(getloc()).widen(c)
       *  @endcode
       *
       *  Additional l10n notes are at
       *  http://gcc.gnu.org/onlinedocs/libstdc++/22_locale/howto.html
      */
      char_type
      widen(char __c) const;

    protected:
      // 27.4.5.1  basic_ios constructors
      /**
       *  @brief  Empty.
       *
       *  The default constructor does nothing and is not normally
       *  accessible to users.
      */
      basic_ios()
      : ios_base(), _M_tie(0), _M_fill(char_type()), _M_fill_init(false), 
      _M_streambuf(0), _M_ctype(0), _M_num_put(0), _M_num_get(0)
      { }

      /**
       *  @brief  All setup is performed here.
       *
       *  This is called from the public constructor.  It is not virtual and
       *  cannot be redefined.
      */
      void
      init(basic_streambuf<_CharT, _Traits>* __sb);

      void
      _M_cache_locale(const locale& __loc);
    };

_GLIBCXX_END_NAMESPACE

#ifndef _GLIBCXX_EXPORT_TEMPLATE
#include <bits/basic_ios.tcc>
#endif

#endif /* _BASIC_IOS_H */
