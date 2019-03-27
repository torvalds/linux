// Copyright (c) 2007 The NetBSD Foundation, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
// CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
// IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#if !defined(ATF_CXX_DETAIL_AUTO_ARRAY_HPP)
#define ATF_CXX_DETAIL_AUTO_ARRAY_HPP

#include <cstddef>

namespace atf {

// ------------------------------------------------------------------------
// The "auto_array" class.
// ------------------------------------------------------------------------

template< class T >
struct auto_array_ref {
    T* m_ptr;

    explicit auto_array_ref(T*);
};

template< class T >
auto_array_ref< T >::auto_array_ref(T* ptr) :
    m_ptr(ptr)
{
}

template< class T >
class auto_array {
    T* m_ptr;

public:
    auto_array(T* = NULL) throw();
    auto_array(auto_array< T >&) throw();
    auto_array(auto_array_ref< T >) throw();
    ~auto_array(void) throw();

    T* get(void) throw();
    const T* get(void) const throw();
    T* release(void) throw();
    void reset(T* = NULL) throw();

    auto_array< T >& operator=(auto_array< T >&) throw();
    auto_array< T >& operator=(auto_array_ref< T >) throw();

    T& operator[](int) throw();
    operator auto_array_ref< T >(void) throw();
};

template< class T >
auto_array< T >::auto_array(T* ptr)
    throw() :
    m_ptr(ptr)
{
}

template< class T >
auto_array< T >::auto_array(auto_array< T >& ptr)
    throw() :
    m_ptr(ptr.release())
{
}

template< class T >
auto_array< T >::auto_array(auto_array_ref< T > ref)
    throw() :
    m_ptr(ref.m_ptr)
{
}

template< class T >
auto_array< T >::~auto_array(void)
    throw()
{
    if (m_ptr != NULL)
        delete [] m_ptr;
}

template< class T >
T*
auto_array< T >::get(void)
    throw()
{
    return m_ptr;
}

template< class T >
const T*
auto_array< T >::get(void)
    const throw()
{
    return m_ptr;
}

template< class T >
T*
auto_array< T >::release(void)
    throw()
{
    T* ptr = m_ptr;
    m_ptr = NULL;
    return ptr;
}

template< class T >
void
auto_array< T >::reset(T* ptr)
    throw()
{
    if (m_ptr != NULL)
        delete [] m_ptr;
    m_ptr = ptr;
}

template< class T >
auto_array< T >&
auto_array< T >::operator=(auto_array< T >& ptr)
    throw()
{
    reset(ptr.release());
    return *this;
}

template< class T >
auto_array< T >&
auto_array< T >::operator=(auto_array_ref< T > ref)
    throw()
{
    if (m_ptr != ref.m_ptr) {
        delete [] m_ptr;
        m_ptr = ref.m_ptr;
    }
    return *this;
}

template< class T >
T&
auto_array< T >::operator[](int pos)
    throw()
{
    return m_ptr[pos];
}

template< class T >
auto_array< T >::operator auto_array_ref< T >(void)
    throw()
{
    return auto_array_ref< T >(release());
}

} // namespace atf

#endif // !defined(ATF_CXX_DETAIL_AUTO_ARRAY_HPP)
