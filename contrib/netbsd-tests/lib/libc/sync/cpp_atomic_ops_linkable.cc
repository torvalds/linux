/* $NetBSD: cpp_atomic_ops_linkable.cc,v 1.5 2017/01/11 12:10:26 joerg Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Martin Husemann <martin@NetBSD.org>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is a simple link-time test to verify all builtin atomic sync
 * operations for C++ <atomic> are available.
 */

#include <atomic>
#include <machine/types.h>	// for __HAVE_ATOMIC64_OPS

template <class T>
class ATest {
public:
  ATest() : m_val(0)
  {
    m_val.exchange(std::atomic<T>(8));
    m_val--;
    m_val++;
    m_val ^= 0x0f;
    m_val &= 0x0f;
    m_val |= 2;

    T tval(1), other(42);
    m_val.compare_exchange_weak(tval, other,
      std::memory_order_release, std::memory_order_relaxed);
  }

private:
  volatile std::atomic<T> m_val;
};

int main(int argc, char **argv)
{
  ATest<char>();
  ATest<signed char>();
  ATest<unsigned char>();
  ATest<short>();
  ATest<unsigned short>();
  ATest<int>();
  ATest<unsigned int>();
  ATest<long>();
  ATest<unsigned long>();
#ifdef __HAVE_ATOMIC64_OPS
  ATest<long long>();
  ATest<unsigned long long>();
#endif
  ATest<char16_t>();
  ATest<char32_t>();
  ATest<wchar_t>();
  ATest<int_least8_t>();
  ATest<uint_least8_t>();
  ATest<int_least16_t>();
  ATest<uint_least16_t>();
  ATest<int_least32_t>();
  ATest<uint_least32_t>();
#ifdef __HAVE_ATOMIC64_OPS
  ATest<int_least64_t>();
  ATest<uint_least64_t>();
#endif
  ATest<int_fast8_t>();
  ATest<uint_fast8_t>();
  ATest<int_fast16_t>();
  ATest<uint_fast16_t>();
  ATest<int_fast32_t>();
  ATest<uint_fast32_t>();
#ifdef __HAVE_ATOMIC64_OPS
  ATest<int_fast64_t>();
  ATest<uint_fast64_t>();
#endif
  ATest<intptr_t>();
  ATest<uintptr_t>();
  ATest<std::size_t>();
  ATest<std::ptrdiff_t>();
#ifdef __HAVE_ATOMIC64_OPS
  ATest<intmax_t>();
  ATest<uintmax_t>();
#endif
}
