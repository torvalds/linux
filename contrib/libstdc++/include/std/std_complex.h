// The template and inlines for the -*- C++ -*- complex number classes.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
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

/** @file complex
 *  This is a Standard C++ Library header.
 */

//
// ISO C++ 14882: 26.2  Complex Numbers
// Note: this is not a conforming implementation.
// Initially implemented by Ulrich Drepper <drepper@cygnus.com>
// Improved by Gabriel Dos Reis <dosreis@cmla.ens-cachan.fr>
//

#ifndef _GLIBCXX_COMPLEX
#define _GLIBCXX_COMPLEX 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <bits/cpp_type_traits.h>
#include <cmath>
#include <sstream>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // Forward declarations.
  template<typename _Tp> class complex;
  template<> class complex<float>;
  template<> class complex<double>;
  template<> class complex<long double>;

  ///  Return magnitude of @a z.
  template<typename _Tp> _Tp abs(const complex<_Tp>&);
  ///  Return phase angle of @a z.
  template<typename _Tp> _Tp arg(const complex<_Tp>&);
  ///  Return @a z magnitude squared.
  template<typename _Tp> _Tp norm(const complex<_Tp>&);

  ///  Return complex conjugate of @a z.
  template<typename _Tp> complex<_Tp> conj(const complex<_Tp>&);
  ///  Return complex with magnitude @a rho and angle @a theta.
  template<typename _Tp> complex<_Tp> polar(const _Tp&, const _Tp& = 0);

  // Transcendentals:
  /// Return complex cosine of @a z.
  template<typename _Tp> complex<_Tp> cos(const complex<_Tp>&);
  /// Return complex hyperbolic cosine of @a z.
  template<typename _Tp> complex<_Tp> cosh(const complex<_Tp>&);
  /// Return complex base e exponential of @a z.
  template<typename _Tp> complex<_Tp> exp(const complex<_Tp>&);
  /// Return complex natural logarithm of @a z.
  template<typename _Tp> complex<_Tp> log(const complex<_Tp>&);
  /// Return complex base 10 logarithm of @a z.
  template<typename _Tp> complex<_Tp> log10(const complex<_Tp>&);
  /// Return complex cosine of @a z.
  template<typename _Tp> complex<_Tp> pow(const complex<_Tp>&, int);
  /// Return @a x to the @a y'th power.
  template<typename _Tp> complex<_Tp> pow(const complex<_Tp>&, const _Tp&);
  /// Return @a x to the @a y'th power.
  template<typename _Tp> complex<_Tp> pow(const complex<_Tp>&, 
                                          const complex<_Tp>&);
  /// Return @a x to the @a y'th power.
  template<typename _Tp> complex<_Tp> pow(const _Tp&, const complex<_Tp>&);
  /// Return complex sine of @a z.
  template<typename _Tp> complex<_Tp> sin(const complex<_Tp>&);
  /// Return complex hyperbolic sine of @a z.
  template<typename _Tp> complex<_Tp> sinh(const complex<_Tp>&);
  /// Return complex square root of @a z.
  template<typename _Tp> complex<_Tp> sqrt(const complex<_Tp>&);
  /// Return complex tangent of @a z.
  template<typename _Tp> complex<_Tp> tan(const complex<_Tp>&);
  /// Return complex hyperbolic tangent of @a z.
  template<typename _Tp> complex<_Tp> tanh(const complex<_Tp>&);
  //@}
    
    
  // 26.2.2  Primary template class complex
  /**
   *  Template to represent complex numbers.
   *
   *  Specializations for float, double, and long double are part of the
   *  library.  Results with any other type are not guaranteed.
   *
   *  @param  Tp  Type of real and imaginary values.
  */
  template<typename _Tp>
    struct complex
    {
      /// Value typedef.
      typedef _Tp value_type;
      
      ///  Default constructor.  First parameter is x, second parameter is y.
      ///  Unspecified parameters default to 0.
      complex(const _Tp& = _Tp(), const _Tp & = _Tp());

      // Lets the compiler synthesize the copy constructor   
      // complex (const complex<_Tp>&);
      ///  Copy constructor.
      template<typename _Up>
        complex(const complex<_Up>&);

      ///  Return real part of complex number.
      _Tp& real(); 
      ///  Return real part of complex number.
      const _Tp& real() const;
      ///  Return imaginary part of complex number.
      _Tp& imag();
      ///  Return imaginary part of complex number.
      const _Tp& imag() const;

      /// Assign this complex number to scalar @a t.
      complex<_Tp>& operator=(const _Tp&);
      /// Add @a t to this complex number.
      complex<_Tp>& operator+=(const _Tp&);
      /// Subtract @a t from this complex number.
      complex<_Tp>& operator-=(const _Tp&);
      /// Multiply this complex number by @a t.
      complex<_Tp>& operator*=(const _Tp&);
      /// Divide this complex number by @a t.
      complex<_Tp>& operator/=(const _Tp&);

      // Lets the compiler synthesize the
      // copy and assignment operator
      // complex<_Tp>& operator= (const complex<_Tp>&);
      /// Assign this complex number to complex @a z.
      template<typename _Up>
        complex<_Tp>& operator=(const complex<_Up>&);
      /// Add @a z to this complex number.
      template<typename _Up>
        complex<_Tp>& operator+=(const complex<_Up>&);
      /// Subtract @a z from this complex number.
      template<typename _Up>
        complex<_Tp>& operator-=(const complex<_Up>&);
      /// Multiply this complex number by @a z.
      template<typename _Up>
        complex<_Tp>& operator*=(const complex<_Up>&);
      /// Divide this complex number by @a z.
      template<typename _Up>
        complex<_Tp>& operator/=(const complex<_Up>&);

      const complex& __rep() const;

    private:
      _Tp _M_real;
      _Tp _M_imag;
    };

  template<typename _Tp>
    inline _Tp&
    complex<_Tp>::real() { return _M_real; }

  template<typename _Tp>
    inline const _Tp&
    complex<_Tp>::real() const { return _M_real; }

  template<typename _Tp>
    inline _Tp&
    complex<_Tp>::imag() { return _M_imag; }

  template<typename _Tp>
    inline const _Tp&
    complex<_Tp>::imag() const { return _M_imag; }

  template<typename _Tp>
    inline 
    complex<_Tp>::complex(const _Tp& __r, const _Tp& __i)
    : _M_real(__r), _M_imag(__i) { }

  template<typename _Tp>
    template<typename _Up>
    inline 
    complex<_Tp>::complex(const complex<_Up>& __z)
    : _M_real(__z.real()), _M_imag(__z.imag()) { }
        
  template<typename _Tp>
    complex<_Tp>&
    complex<_Tp>::operator=(const _Tp& __t)
    {
     _M_real = __t;
     _M_imag = _Tp();
     return *this;
    } 

  // 26.2.5/1
  template<typename _Tp>
    inline complex<_Tp>&
    complex<_Tp>::operator+=(const _Tp& __t)
    {
      _M_real += __t;
      return *this;
    }

  // 26.2.5/3
  template<typename _Tp>
    inline complex<_Tp>&
    complex<_Tp>::operator-=(const _Tp& __t)
    {
      _M_real -= __t;
      return *this;
    }

  // 26.2.5/5
  template<typename _Tp>
    complex<_Tp>&
    complex<_Tp>::operator*=(const _Tp& __t)
    {
      _M_real *= __t;
      _M_imag *= __t;
      return *this;
    }

  // 26.2.5/7
  template<typename _Tp>
    complex<_Tp>&
    complex<_Tp>::operator/=(const _Tp& __t)
    {
      _M_real /= __t;
      _M_imag /= __t;
      return *this;
    }

  template<typename _Tp>
    template<typename _Up>
    complex<_Tp>&
    complex<_Tp>::operator=(const complex<_Up>& __z)
    {
      _M_real = __z.real();
      _M_imag = __z.imag();
      return *this;
    }

  // 26.2.5/9
  template<typename _Tp>
    template<typename _Up>
    complex<_Tp>&
    complex<_Tp>::operator+=(const complex<_Up>& __z)
    {
      _M_real += __z.real();
      _M_imag += __z.imag();
      return *this;
    }

  // 26.2.5/11
  template<typename _Tp>
    template<typename _Up>
    complex<_Tp>&
    complex<_Tp>::operator-=(const complex<_Up>& __z)
    {
      _M_real -= __z.real();
      _M_imag -= __z.imag();
      return *this;
    }

  // 26.2.5/13
  // XXX: This is a grammar school implementation.
  template<typename _Tp>
    template<typename _Up>
    complex<_Tp>&
    complex<_Tp>::operator*=(const complex<_Up>& __z)
    {
      const _Tp __r = _M_real * __z.real() - _M_imag * __z.imag();
      _M_imag = _M_real * __z.imag() + _M_imag * __z.real();
      _M_real = __r;
      return *this;
    }

  // 26.2.5/15
  // XXX: This is a grammar school implementation.
  template<typename _Tp>
    template<typename _Up>
    complex<_Tp>&
    complex<_Tp>::operator/=(const complex<_Up>& __z)
    {
      const _Tp __r =  _M_real * __z.real() + _M_imag * __z.imag();
      const _Tp __n = std::norm(__z);
      _M_imag = (_M_imag * __z.real() - _M_real * __z.imag()) / __n;
      _M_real = __r / __n;
      return *this;
    }

  template<typename _Tp>
    inline const complex<_Tp>&
    complex<_Tp>::__rep() const { return *this; }
    
  // Operators:
  //@{
  ///  Return new complex value @a x plus @a y.
  template<typename _Tp>
    inline complex<_Tp>
    operator+(const complex<_Tp>& __x, const complex<_Tp>& __y)
    {
      complex<_Tp> __r = __x;
      __r += __y;
      return __r;
    }

  template<typename _Tp>
    inline complex<_Tp>
    operator+(const complex<_Tp>& __x, const _Tp& __y)
    {
      complex<_Tp> __r = __x;
      __r.real() += __y;
      return __r;
    }

  template<typename _Tp>
    inline complex<_Tp>
    operator+(const _Tp& __x, const complex<_Tp>& __y)
    {
      complex<_Tp> __r = __y;
      __r.real() += __x;
      return __r;
    }
  //@}

  //@{
  ///  Return new complex value @a x minus @a y.
  template<typename _Tp>
    inline complex<_Tp>
    operator-(const complex<_Tp>& __x, const complex<_Tp>& __y)
    {
      complex<_Tp> __r = __x;
      __r -= __y;
      return __r;
    }
    
  template<typename _Tp>
    inline complex<_Tp>
    operator-(const complex<_Tp>& __x, const _Tp& __y)
    {
      complex<_Tp> __r = __x;
      __r.real() -= __y;
      return __r;
    }

  template<typename _Tp>
    inline complex<_Tp>
    operator-(const _Tp& __x, const complex<_Tp>& __y)
    {
      complex<_Tp> __r(__x, -__y.imag());
      __r.real() -= __y.real();
      return __r;
    }
  //@}

  //@{
  ///  Return new complex value @a x times @a y.
  template<typename _Tp>
    inline complex<_Tp>
    operator*(const complex<_Tp>& __x, const complex<_Tp>& __y)
    {
      complex<_Tp> __r = __x;
      __r *= __y;
      return __r;
    }

  template<typename _Tp>
    inline complex<_Tp>
    operator*(const complex<_Tp>& __x, const _Tp& __y)
    {
      complex<_Tp> __r = __x;
      __r *= __y;
      return __r;
    }

  template<typename _Tp>
    inline complex<_Tp>
    operator*(const _Tp& __x, const complex<_Tp>& __y)
    {
      complex<_Tp> __r = __y;
      __r *= __x;
      return __r;
    }
  //@}

  //@{
  ///  Return new complex value @a x divided by @a y.
  template<typename _Tp>
    inline complex<_Tp>
    operator/(const complex<_Tp>& __x, const complex<_Tp>& __y)
    {
      complex<_Tp> __r = __x;
      __r /= __y;
      return __r;
    }
    
  template<typename _Tp>
    inline complex<_Tp>
    operator/(const complex<_Tp>& __x, const _Tp& __y)
    {
      complex<_Tp> __r = __x;
      __r /= __y;
      return __r;
    }

  template<typename _Tp>
    inline complex<_Tp>
    operator/(const _Tp& __x, const complex<_Tp>& __y)
    {
      complex<_Tp> __r = __x;
      __r /= __y;
      return __r;
    }
  //@}

  ///  Return @a x.
  template<typename _Tp>
    inline complex<_Tp>
    operator+(const complex<_Tp>& __x)
    { return __x; }

  ///  Return complex negation of @a x.
  template<typename _Tp>
    inline complex<_Tp>
    operator-(const complex<_Tp>& __x)
    {  return complex<_Tp>(-__x.real(), -__x.imag()); }

  //@{
  ///  Return true if @a x is equal to @a y.
  template<typename _Tp>
    inline bool
    operator==(const complex<_Tp>& __x, const complex<_Tp>& __y)
    { return __x.real() == __y.real() && __x.imag() == __y.imag(); }

  template<typename _Tp>
    inline bool
    operator==(const complex<_Tp>& __x, const _Tp& __y)
    { return __x.real() == __y && __x.imag() == _Tp(); }

  template<typename _Tp>
    inline bool
    operator==(const _Tp& __x, const complex<_Tp>& __y)
    { return __x == __y.real() && _Tp() == __y.imag(); }
  //@}

  //@{
  ///  Return false if @a x is equal to @a y.
  template<typename _Tp>
    inline bool
    operator!=(const complex<_Tp>& __x, const complex<_Tp>& __y)
    { return __x.real() != __y.real() || __x.imag() != __y.imag(); }

  template<typename _Tp>
    inline bool
    operator!=(const complex<_Tp>& __x, const _Tp& __y)
    { return __x.real() != __y || __x.imag() != _Tp(); }

  template<typename _Tp>
    inline bool
    operator!=(const _Tp& __x, const complex<_Tp>& __y)
    { return __x != __y.real() || _Tp() != __y.imag(); }
  //@}

  ///  Extraction operator for complex values.
  template<typename _Tp, typename _CharT, class _Traits>
    basic_istream<_CharT, _Traits>&
    operator>>(basic_istream<_CharT, _Traits>& __is, complex<_Tp>& __x)
    {
      _Tp __re_x, __im_x;
      _CharT __ch;
      __is >> __ch;
      if (__ch == '(') 
	{
	  __is >> __re_x >> __ch;
	  if (__ch == ',') 
	    {
	      __is >> __im_x >> __ch;
	      if (__ch == ')') 
		__x = complex<_Tp>(__re_x, __im_x);
	      else
		__is.setstate(ios_base::failbit);
	    }
	  else if (__ch == ')') 
	    __x = __re_x;
	  else
	    __is.setstate(ios_base::failbit);
	}
      else 
	{
	  __is.putback(__ch);
	  __is >> __re_x;
	  __x = __re_x;
	}
      return __is;
    }

  ///  Insertion operator for complex values.
  template<typename _Tp, typename _CharT, class _Traits>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __os, const complex<_Tp>& __x)
    {
      basic_ostringstream<_CharT, _Traits> __s;
      __s.flags(__os.flags());
      __s.imbue(__os.getloc());
      __s.precision(__os.precision());
      __s << '(' << __x.real() << ',' << __x.imag() << ')';
      return __os << __s.str();
    }

  // Values
  template<typename _Tp>
    inline _Tp&
    real(complex<_Tp>& __z)
    { return __z.real(); }
    
  template<typename _Tp>
    inline const _Tp&
    real(const complex<_Tp>& __z)
    { return __z.real(); }
    
  template<typename _Tp>
    inline _Tp&
    imag(complex<_Tp>& __z)
    { return __z.imag(); }
    
  template<typename _Tp>
    inline const _Tp&
    imag(const complex<_Tp>& __z)
    { return __z.imag(); }

  // 26.2.7/3 abs(__z):  Returns the magnitude of __z.
  template<typename _Tp>
    inline _Tp
    __complex_abs(const complex<_Tp>& __z)
    {
      _Tp __x = __z.real();
      _Tp __y = __z.imag();
      const _Tp __s = std::max(abs(__x), abs(__y));
      if (__s == _Tp())  // well ...
        return __s;
      __x /= __s; 
      __y /= __s;
      return __s * sqrt(__x * __x + __y * __y);
    }

#if _GLIBCXX_USE_C99_COMPLEX
  inline float
  __complex_abs(__complex__ float __z) { return __builtin_cabsf(__z); }

  inline double
  __complex_abs(__complex__ double __z) { return __builtin_cabs(__z); }

  inline long double
  __complex_abs(const __complex__ long double& __z)
  { return __builtin_cabsl(__z); }

  template<typename _Tp>
    inline _Tp
    abs(const complex<_Tp>& __z) { return __complex_abs(__z.__rep()); }
#else
  template<typename _Tp>
    inline _Tp
    abs(const complex<_Tp>& __z) { return __complex_abs(__z); }
#endif  


  // 26.2.7/4: arg(__z): Returns the phase angle of __z.
  template<typename _Tp>
    inline _Tp
    __complex_arg(const complex<_Tp>& __z)
    { return  atan2(__z.imag(), __z.real()); }

#if _GLIBCXX_USE_C99_COMPLEX
  inline float
  __complex_arg(__complex__ float __z) { return __builtin_cargf(__z); }

  inline double
  __complex_arg(__complex__ double __z) { return __builtin_carg(__z); }

  inline long double
  __complex_arg(const __complex__ long double& __z)
  { return __builtin_cargl(__z); }

  template<typename _Tp>
    inline _Tp
    arg(const complex<_Tp>& __z) { return __complex_arg(__z.__rep()); }
#else
  template<typename _Tp>
    inline _Tp
    arg(const complex<_Tp>& __z) { return __complex_arg(__z); }
#endif

  // 26.2.7/5: norm(__z) returns the squared magintude of __z.
  //     As defined, norm() is -not- a norm is the common mathematical
  //     sens used in numerics.  The helper class _Norm_helper<> tries to
  //     distinguish between builtin floating point and the rest, so as
  //     to deliver an answer as close as possible to the real value.
  template<bool>
    struct _Norm_helper
    {
      template<typename _Tp>
        static inline _Tp _S_do_it(const complex<_Tp>& __z)
        {
          const _Tp __x = __z.real();
          const _Tp __y = __z.imag();
          return __x * __x + __y * __y;
        }
    };

  template<>
    struct _Norm_helper<true>
    {
      template<typename _Tp>
        static inline _Tp _S_do_it(const complex<_Tp>& __z)
        {
          _Tp __res = std::abs(__z);
          return __res * __res;
        }
    };
  
  template<typename _Tp>
    inline _Tp
    norm(const complex<_Tp>& __z)
    {
      return _Norm_helper<__is_floating<_Tp>::__value 
	&& !_GLIBCXX_FAST_MATH>::_S_do_it(__z);
    }

  template<typename _Tp>
    inline complex<_Tp>
    polar(const _Tp& __rho, const _Tp& __theta)
    { return complex<_Tp>(__rho * cos(__theta), __rho * sin(__theta)); }

  template<typename _Tp>
    inline complex<_Tp>
    conj(const complex<_Tp>& __z)
    { return complex<_Tp>(__z.real(), -__z.imag()); }
  
  // Transcendentals

  // 26.2.8/1 cos(__z):  Returns the cosine of __z.
  template<typename _Tp>
    inline complex<_Tp>
    __complex_cos(const complex<_Tp>& __z)
    {
      const _Tp __x = __z.real();
      const _Tp __y = __z.imag();
      return complex<_Tp>(cos(__x) * cosh(__y), -sin(__x) * sinh(__y));
    }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_cos(__complex__ float __z) { return __builtin_ccosf(__z); }

  inline __complex__ double
  __complex_cos(__complex__ double __z) { return __builtin_ccos(__z); }

  inline __complex__ long double
  __complex_cos(const __complex__ long double& __z)
  { return __builtin_ccosl(__z); }

  template<typename _Tp>
    inline complex<_Tp>
    cos(const complex<_Tp>& __z) { return __complex_cos(__z.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    cos(const complex<_Tp>& __z) { return __complex_cos(__z); }
#endif

  // 26.2.8/2 cosh(__z): Returns the hyperbolic cosine of __z.
  template<typename _Tp>
    inline complex<_Tp>
    __complex_cosh(const complex<_Tp>& __z)
    {
      const _Tp __x = __z.real();
      const _Tp __y = __z.imag();
      return complex<_Tp>(cosh(__x) * cos(__y), sinh(__x) * sin(__y));
    }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_cosh(__complex__ float __z) { return __builtin_ccoshf(__z); }

  inline __complex__ double
  __complex_cosh(__complex__ double __z) { return __builtin_ccosh(__z); }

  inline __complex__ long double
  __complex_cosh(const __complex__ long double& __z)
  { return __builtin_ccoshl(__z); }

  template<typename _Tp>
    inline complex<_Tp>
    cosh(const complex<_Tp>& __z) { return __complex_cosh(__z.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    cosh(const complex<_Tp>& __z) { return __complex_cosh(__z); }
#endif

  // 26.2.8/3 exp(__z): Returns the complex base e exponential of x
  template<typename _Tp>
    inline complex<_Tp>
    __complex_exp(const complex<_Tp>& __z)
    { return std::polar(exp(__z.real()), __z.imag()); }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_exp(__complex__ float __z) { return __builtin_cexpf(__z); }

  inline __complex__ double
  __complex_exp(__complex__ double __z) { return __builtin_cexp(__z); }

  inline __complex__ long double
  __complex_exp(const __complex__ long double& __z)
  { return __builtin_cexpl(__z); }

  template<typename _Tp>
    inline complex<_Tp>
    exp(const complex<_Tp>& __z) { return __complex_exp(__z.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    exp(const complex<_Tp>& __z) { return __complex_exp(__z); }
#endif

  // 26.2.8/5 log(__z): Reurns the natural complex logaritm of __z.
  //                    The branch cut is along the negative axis.
  template<typename _Tp>
    inline complex<_Tp>
    __complex_log(const complex<_Tp>& __z)
    { return complex<_Tp>(log(std::abs(__z)), std::arg(__z)); }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_log(__complex__ float __z) { return __builtin_clogf(__z); }

  inline __complex__ double
  __complex_log(__complex__ double __z) { return __builtin_clog(__z); }

  inline __complex__ long double
  __complex_log(const __complex__ long double& __z)
  { return __builtin_clogl(__z); }

  template<typename _Tp>
    inline complex<_Tp>
    log(const complex<_Tp>& __z) { return __complex_log(__z.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    log(const complex<_Tp>& __z) { return __complex_log(__z); }
#endif

  template<typename _Tp>
    inline complex<_Tp>
    log10(const complex<_Tp>& __z)
    { return std::log(__z) / log(_Tp(10.0)); }

  // 26.2.8/10 sin(__z): Returns the sine of __z.
  template<typename _Tp>
    inline complex<_Tp>
    __complex_sin(const complex<_Tp>& __z)
    {
      const _Tp __x = __z.real();
      const _Tp __y = __z.imag();
      return complex<_Tp>(sin(__x) * cosh(__y), cos(__x) * sinh(__y)); 
    }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_sin(__complex__ float __z) { return __builtin_csinf(__z); }

  inline __complex__ double
  __complex_sin(__complex__ double __z) { return __builtin_csin(__z); }

  inline __complex__ long double
  __complex_sin(const __complex__ long double& __z)
  { return __builtin_csinl(__z); }

  template<typename _Tp>
    inline complex<_Tp>
    sin(const complex<_Tp>& __z) { return __complex_sin(__z.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    sin(const complex<_Tp>& __z) { return __complex_sin(__z); }
#endif

  // 26.2.8/11 sinh(__z): Returns the hyperbolic sine of __z.
  template<typename _Tp>
    inline complex<_Tp>
    __complex_sinh(const complex<_Tp>& __z)
    {
      const _Tp __x = __z.real();
      const _Tp  __y = __z.imag();
      return complex<_Tp>(sinh(__x) * cos(__y), cosh(__x) * sin(__y));
    }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_sinh(__complex__ float __z) { return __builtin_csinhf(__z); }      

  inline __complex__ double
  __complex_sinh(__complex__ double __z) { return __builtin_csinh(__z); }      

  inline __complex__ long double
  __complex_sinh(const __complex__ long double& __z)
  { return __builtin_csinhl(__z); }      

  template<typename _Tp>
    inline complex<_Tp>
    sinh(const complex<_Tp>& __z) { return __complex_sinh(__z.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    sinh(const complex<_Tp>& __z) { return __complex_sinh(__z); }
#endif

  // 26.2.8/13 sqrt(__z): Returns the complex square root of __z.
  //                     The branch cut is on the negative axis.
  template<typename _Tp>
    complex<_Tp>
    __complex_sqrt(const complex<_Tp>& __z)
    {
      _Tp __x = __z.real();
      _Tp __y = __z.imag();

      if (__x == _Tp())
        {
          _Tp __t = sqrt(abs(__y) / 2);
          return complex<_Tp>(__t, __y < _Tp() ? -__t : __t);
        }
      else
        {
          _Tp __t = sqrt(2 * (std::abs(__z) + abs(__x)));
          _Tp __u = __t / 2;
          return __x > _Tp()
            ? complex<_Tp>(__u, __y / __t)
            : complex<_Tp>(abs(__y) / __t, __y < _Tp() ? -__u : __u);
        }
    }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_sqrt(__complex__ float __z) { return __builtin_csqrtf(__z); }

  inline __complex__ double
  __complex_sqrt(__complex__ double __z) { return __builtin_csqrt(__z); }

  inline __complex__ long double
  __complex_sqrt(const __complex__ long double& __z)
  { return __builtin_csqrtl(__z); }

  template<typename _Tp>
    inline complex<_Tp>
    sqrt(const complex<_Tp>& __z) { return __complex_sqrt(__z.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    sqrt(const complex<_Tp>& __z) { return __complex_sqrt(__z); }
#endif

  // 26.2.8/14 tan(__z):  Return the complex tangent of __z.
  
  template<typename _Tp>
    inline complex<_Tp>
    __complex_tan(const complex<_Tp>& __z)
    { return std::sin(__z) / std::cos(__z); }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_tan(__complex__ float __z) { return __builtin_ctanf(__z); }

  inline __complex__ double
  __complex_tan(__complex__ double __z) { return __builtin_ctan(__z); }

  inline __complex__ long double
  __complex_tan(const __complex__ long double& __z)
  { return __builtin_ctanl(__z); }

  template<typename _Tp>
    inline complex<_Tp>
    tan(const complex<_Tp>& __z) { return __complex_tan(__z.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    tan(const complex<_Tp>& __z) { return __complex_tan(__z); }
#endif


  // 26.2.8/15 tanh(__z):  Returns the hyperbolic tangent of __z.
  
  template<typename _Tp>
    inline complex<_Tp>
    __complex_tanh(const complex<_Tp>& __z)
    { return std::sinh(__z) / std::cosh(__z); }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_tanh(__complex__ float __z) { return __builtin_ctanhf(__z); }

  inline __complex__ double
  __complex_tanh(__complex__ double __z) { return __builtin_ctanh(__z); }

  inline __complex__ long double
  __complex_tanh(const __complex__ long double& __z)
  { return __builtin_ctanhl(__z); }

  template<typename _Tp>
    inline complex<_Tp>
    tanh(const complex<_Tp>& __z) { return __complex_tanh(__z.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    tanh(const complex<_Tp>& __z) { return __complex_tanh(__z); }
#endif


  // 26.2.8/9  pow(__x, __y): Returns the complex power base of __x
  //                          raised to the __y-th power.  The branch
  //                          cut is on the negative axis.
  template<typename _Tp>
    inline complex<_Tp>
    pow(const complex<_Tp>& __z, int __n)
    { return std::__pow_helper(__z, __n); }

  template<typename _Tp>
    complex<_Tp>
    pow(const complex<_Tp>& __x, const _Tp& __y)
    {
#ifndef _GLIBCXX_USE_C99_COMPLEX
      if (__x == _Tp())
	return _Tp();
#endif
      if (__x.imag() == _Tp() && __x.real() > _Tp())
        return pow(__x.real(), __y);

      complex<_Tp> __t = std::log(__x);
      return std::polar(exp(__y * __t.real()), __y * __t.imag());
    }

  template<typename _Tp>
    inline complex<_Tp>
    __complex_pow(const complex<_Tp>& __x, const complex<_Tp>& __y)
    { return __x == _Tp() ? _Tp() : std::exp(__y * std::log(__x)); }

#if _GLIBCXX_USE_C99_COMPLEX
  inline __complex__ float
  __complex_pow(__complex__ float __x, __complex__ float __y)
  { return __builtin_cpowf(__x, __y); }

  inline __complex__ double
  __complex_pow(__complex__ double __x, __complex__ double __y)
  { return __builtin_cpow(__x, __y); }

  inline __complex__ long double
  __complex_pow(const __complex__ long double& __x,
		const __complex__ long double& __y)
  { return __builtin_cpowl(__x, __y); }

  template<typename _Tp>
    inline complex<_Tp>
    pow(const complex<_Tp>& __x, const complex<_Tp>& __y)
    { return __complex_pow(__x.__rep(), __y.__rep()); }
#else
  template<typename _Tp>
    inline complex<_Tp>
    pow(const complex<_Tp>& __x, const complex<_Tp>& __y)
    { return __complex_pow(__x, __y); }
#endif

  template<typename _Tp>
    inline complex<_Tp>
    pow(const _Tp& __x, const complex<_Tp>& __y)
    {
      return __x > _Tp() ? std::polar(pow(__x, __y.real()),
				      __y.imag() * log(__x))
	                 : std::pow(complex<_Tp>(__x, _Tp()), __y);
    }

  // 26.2.3  complex specializations
  // complex<float> specialization
  template<>
    struct complex<float>
    {
      typedef float value_type;
      typedef __complex__ float _ComplexT;

      complex(_ComplexT __z) : _M_value(__z) { }

      complex(float = 0.0f, float = 0.0f);

      explicit complex(const complex<double>&);
      explicit complex(const complex<long double>&);

      float& real();
      const float& real() const;
      float& imag();
      const float& imag() const;

      complex<float>& operator=(float);
      complex<float>& operator+=(float);
      complex<float>& operator-=(float);
      complex<float>& operator*=(float);
      complex<float>& operator/=(float);

      // Let's the compiler synthetize the copy and assignment
      // operator.  It always does a pretty good job.
      // complex& operator= (const complex&);
      template<typename _Tp>
        complex<float>&operator=(const complex<_Tp>&);
      template<typename _Tp>
        complex<float>& operator+=(const complex<_Tp>&);
      template<class _Tp>
        complex<float>& operator-=(const complex<_Tp>&);
      template<class _Tp>
        complex<float>& operator*=(const complex<_Tp>&);
      template<class _Tp>
        complex<float>&operator/=(const complex<_Tp>&);

      const _ComplexT& __rep() const { return _M_value; }

    private:
      _ComplexT _M_value;
    };

  inline float&
  complex<float>::real()
  { return __real__ _M_value; }

  inline const float&
  complex<float>::real() const
  { return __real__ _M_value; }

  inline float&
  complex<float>::imag()
  { return __imag__ _M_value; }

  inline const float&
  complex<float>::imag() const
  { return __imag__ _M_value; }

  inline
  complex<float>::complex(float r, float i)
  {
    __real__ _M_value = r;
    __imag__ _M_value = i;
  }

  inline complex<float>&
  complex<float>::operator=(float __f)
  {
    __real__ _M_value = __f;
    __imag__ _M_value = 0.0f;
    return *this;
  }

  inline complex<float>&
  complex<float>::operator+=(float __f)
  {
    __real__ _M_value += __f;
    return *this;
  }

  inline complex<float>&
  complex<float>::operator-=(float __f)
  {
    __real__ _M_value -= __f;
    return *this;
  }

  inline complex<float>&
  complex<float>::operator*=(float __f)
  {
    _M_value *= __f;
    return *this;
  }

  inline complex<float>&
  complex<float>::operator/=(float __f)
  {
    _M_value /= __f;
    return *this;
  }

  template<typename _Tp>
  inline complex<float>&
  complex<float>::operator=(const complex<_Tp>& __z)
  {
    __real__ _M_value = __z.real();
    __imag__ _M_value = __z.imag();
    return *this;
  }

  template<typename _Tp>
  inline complex<float>&
  complex<float>::operator+=(const complex<_Tp>& __z)
  {
    __real__ _M_value += __z.real();
    __imag__ _M_value += __z.imag();
    return *this;
  }
    
  template<typename _Tp>
    inline complex<float>&
    complex<float>::operator-=(const complex<_Tp>& __z)
    {
     __real__ _M_value -= __z.real();
     __imag__ _M_value -= __z.imag();
     return *this;
    } 

  template<typename _Tp>
    inline complex<float>&
    complex<float>::operator*=(const complex<_Tp>& __z)
    {
      _ComplexT __t;
      __real__ __t = __z.real();
      __imag__ __t = __z.imag();
      _M_value *= __t;
      return *this;
    }

  template<typename _Tp>
    inline complex<float>&
    complex<float>::operator/=(const complex<_Tp>& __z)
    {
      _ComplexT __t;
      __real__ __t = __z.real();
      __imag__ __t = __z.imag();
      _M_value /= __t;
      return *this;
    }

  // 26.2.3  complex specializations
  // complex<double> specialization
  template<>
    struct complex<double>
    {
      typedef double value_type;
      typedef __complex__ double _ComplexT;

      complex(_ComplexT __z) : _M_value(__z) { }

      complex(double = 0.0, double = 0.0);

      complex(const complex<float>&);
      explicit complex(const complex<long double>&);

      double& real();
      const double& real() const;
      double& imag();
      const double& imag() const;

      complex<double>& operator=(double);
      complex<double>& operator+=(double);
      complex<double>& operator-=(double);
      complex<double>& operator*=(double);
      complex<double>& operator/=(double);

      // The compiler will synthetize this, efficiently.
      // complex& operator= (const complex&);
      template<typename _Tp>
        complex<double>& operator=(const complex<_Tp>&);
      template<typename _Tp>
        complex<double>& operator+=(const complex<_Tp>&);
      template<typename _Tp>
        complex<double>& operator-=(const complex<_Tp>&);
      template<typename _Tp>
        complex<double>& operator*=(const complex<_Tp>&);
      template<typename _Tp>
        complex<double>& operator/=(const complex<_Tp>&);

      const _ComplexT& __rep() const { return _M_value; }

    private:
      _ComplexT _M_value;
    };

  inline double&
  complex<double>::real()
  { return __real__ _M_value; }

  inline const double&
  complex<double>::real() const
  { return __real__ _M_value; }

  inline double&
  complex<double>::imag()
  { return __imag__ _M_value; }

  inline const double&
  complex<double>::imag() const
  { return __imag__ _M_value; }

  inline
  complex<double>::complex(double __r, double __i)
  {
    __real__ _M_value = __r;
    __imag__ _M_value = __i;
  }

  inline complex<double>&
  complex<double>::operator=(double __d)
  {
    __real__ _M_value = __d;
    __imag__ _M_value = 0.0;
    return *this;
  }

  inline complex<double>&
  complex<double>::operator+=(double __d)
  {
    __real__ _M_value += __d;
    return *this;
  }

  inline complex<double>&
  complex<double>::operator-=(double __d)
  {
    __real__ _M_value -= __d;
    return *this;
  }

  inline complex<double>&
  complex<double>::operator*=(double __d)
  {
    _M_value *= __d;
    return *this;
  }

  inline complex<double>&
  complex<double>::operator/=(double __d)
  {
    _M_value /= __d;
    return *this;
  }

  template<typename _Tp>
    inline complex<double>&
    complex<double>::operator=(const complex<_Tp>& __z)
    {
      __real__ _M_value = __z.real();
      __imag__ _M_value = __z.imag();
      return *this;
    }
    
  template<typename _Tp>
    inline complex<double>&
    complex<double>::operator+=(const complex<_Tp>& __z)
    {
      __real__ _M_value += __z.real();
      __imag__ _M_value += __z.imag();
      return *this;
    }

  template<typename _Tp>
    inline complex<double>&
    complex<double>::operator-=(const complex<_Tp>& __z)
    {
      __real__ _M_value -= __z.real();
      __imag__ _M_value -= __z.imag();
      return *this;
    }

  template<typename _Tp>
    inline complex<double>&
    complex<double>::operator*=(const complex<_Tp>& __z)
    {
      _ComplexT __t;
      __real__ __t = __z.real();
      __imag__ __t = __z.imag();
      _M_value *= __t;
      return *this;
    }

  template<typename _Tp>
    inline complex<double>&
    complex<double>::operator/=(const complex<_Tp>& __z)
    {
      _ComplexT __t;
      __real__ __t = __z.real();
      __imag__ __t = __z.imag();
      _M_value /= __t;
      return *this;
    }

  // 26.2.3  complex specializations
  // complex<long double> specialization
  template<>
    struct complex<long double>
    {
      typedef long double value_type;
      typedef __complex__ long double _ComplexT;

      complex(_ComplexT __z) : _M_value(__z) { }

      complex(long double = 0.0L, long double = 0.0L);

      complex(const complex<float>&);
      complex(const complex<double>&);

      long double& real();
      const long double& real() const;
      long double& imag();
      const long double& imag() const;

      complex<long double>& operator= (long double);
      complex<long double>& operator+= (long double);
      complex<long double>& operator-= (long double);
      complex<long double>& operator*= (long double);
      complex<long double>& operator/= (long double);

      // The compiler knows how to do this efficiently
      // complex& operator= (const complex&);
      template<typename _Tp>
        complex<long double>& operator=(const complex<_Tp>&);
      template<typename _Tp>
        complex<long double>& operator+=(const complex<_Tp>&);
      template<typename _Tp>
        complex<long double>& operator-=(const complex<_Tp>&);
      template<typename _Tp>
        complex<long double>& operator*=(const complex<_Tp>&);
      template<typename _Tp>
        complex<long double>& operator/=(const complex<_Tp>&);

      const _ComplexT& __rep() const { return _M_value; }

    private:
      _ComplexT _M_value;
    };

  inline
  complex<long double>::complex(long double __r, long double __i)
  {
    __real__ _M_value = __r;
    __imag__ _M_value = __i;
  }

  inline long double&
  complex<long double>::real()
  { return __real__ _M_value; }

  inline const long double&
  complex<long double>::real() const
  { return __real__ _M_value; }

  inline long double&
  complex<long double>::imag()
  { return __imag__ _M_value; }

  inline const long double&
  complex<long double>::imag() const
  { return __imag__ _M_value; }

  inline complex<long double>&   
  complex<long double>::operator=(long double __r)
  {
    __real__ _M_value = __r;
    __imag__ _M_value = 0.0L;
    return *this;
  }

  inline complex<long double>&
  complex<long double>::operator+=(long double __r)
  {
    __real__ _M_value += __r;
    return *this;
  }

  inline complex<long double>&
  complex<long double>::operator-=(long double __r)
  {
    __real__ _M_value -= __r;
    return *this;
  }

  inline complex<long double>&
  complex<long double>::operator*=(long double __r)
  {
    _M_value *= __r;
    return *this;
  }

  inline complex<long double>&
  complex<long double>::operator/=(long double __r)
  {
    _M_value /= __r;
    return *this;
  }

  template<typename _Tp>
    inline complex<long double>&
    complex<long double>::operator=(const complex<_Tp>& __z)
    {
      __real__ _M_value = __z.real();
      __imag__ _M_value = __z.imag();
      return *this;
    }

  template<typename _Tp>
    inline complex<long double>&
    complex<long double>::operator+=(const complex<_Tp>& __z)
    {
      __real__ _M_value += __z.real();
      __imag__ _M_value += __z.imag();
      return *this;
    }

  template<typename _Tp>
    inline complex<long double>&
    complex<long double>::operator-=(const complex<_Tp>& __z)
    {
      __real__ _M_value -= __z.real();
      __imag__ _M_value -= __z.imag();
      return *this;
    }
    
  template<typename _Tp>
    inline complex<long double>&
    complex<long double>::operator*=(const complex<_Tp>& __z)
    {
      _ComplexT __t;
      __real__ __t = __z.real();
      __imag__ __t = __z.imag();
      _M_value *= __t;
      return *this;
    }

  template<typename _Tp>
    inline complex<long double>&
    complex<long double>::operator/=(const complex<_Tp>& __z)
    {
      _ComplexT __t;
      __real__ __t = __z.real();
      __imag__ __t = __z.imag();
      _M_value /= __t;
      return *this;
    }

  // These bits have to be at the end of this file, so that the
  // specializations have all been defined.
  // ??? No, they have to be there because of compiler limitation at
  // inlining.  It suffices that class specializations be defined.
  inline
  complex<float>::complex(const complex<double>& __z)
  : _M_value(__z.__rep()) { }

  inline
  complex<float>::complex(const complex<long double>& __z)
  : _M_value(__z.__rep()) { }

  inline
  complex<double>::complex(const complex<float>& __z) 
  : _M_value(__z.__rep()) { }

  inline
  complex<double>::complex(const complex<long double>& __z)
    : _M_value(__z.__rep()) { }

  inline
  complex<long double>::complex(const complex<float>& __z)
  : _M_value(__z.__rep()) { }

  inline
  complex<long double>::complex(const complex<double>& __z)
  : _M_value(__z.__rep()) { }

_GLIBCXX_END_NAMESPACE

#endif	/* _GLIBCXX_COMPLEX */
