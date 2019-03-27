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

#include "atf-c++/detail/auto_array.hpp"

extern "C" {
#include <sys/types.h>
}

#include <iostream>

#include <atf-c++.hpp>

extern "C" {
#include "atf-c/defs.h"
}

// ------------------------------------------------------------------------
// Tests for the "auto_array" class.
// ------------------------------------------------------------------------

class test_array {
public:
    int m_value;

    static ssize_t m_nblocks;

    static
    atf::auto_array< test_array >
    do_copy(atf::auto_array< test_array >& ta)
    {
        return atf::auto_array< test_array >(ta);
    }

    void* operator new(size_t size ATF_DEFS_ATTRIBUTE_UNUSED)
    {
        ATF_FAIL("New called but should have been new[]");
        return new int(5);
    }

    void* operator new[](size_t size)
    {
        m_nblocks++;
        void* mem = ::operator new(size);
        std::cout << "Allocated 'test_array' object " << mem << "\n";
        return mem;
    }

    void operator delete(void* mem ATF_DEFS_ATTRIBUTE_UNUSED)
    {
        ATF_FAIL("Delete called but should have been delete[]");
    }

    void operator delete[](void* mem)
    {
        std::cout << "Releasing 'test_array' object " << mem << "\n";
        if (m_nblocks == 0)
            ATF_FAIL("Unbalanced delete[]");
        m_nblocks--;
        ::operator delete(mem);
    }
};

ssize_t test_array::m_nblocks = 0;

ATF_TEST_CASE(auto_array_scope);
ATF_TEST_CASE_HEAD(auto_array_scope)
{
    set_md_var("descr", "Tests the automatic scope handling in the "
               "auto_array smart pointer class");
}
ATF_TEST_CASE_BODY(auto_array_scope)
{
    using atf::auto_array;

    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    {
        auto_array< test_array > t(new test_array[10]);
        ATF_REQUIRE_EQ(test_array::m_nblocks, 1);
    }
    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
}

ATF_TEST_CASE(auto_array_copy);
ATF_TEST_CASE_HEAD(auto_array_copy)
{
    set_md_var("descr", "Tests the auto_array smart pointer class' copy "
               "constructor");
}
ATF_TEST_CASE_BODY(auto_array_copy)
{
    using atf::auto_array;

    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    {
        auto_array< test_array > t1(new test_array[10]);
        ATF_REQUIRE_EQ(test_array::m_nblocks, 1);

        {
            auto_array< test_array > t2(t1);
            ATF_REQUIRE_EQ(test_array::m_nblocks, 1);
        }
        ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    }
    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
}

ATF_TEST_CASE(auto_array_copy_ref);
ATF_TEST_CASE_HEAD(auto_array_copy_ref)
{
    set_md_var("descr", "Tests the auto_array smart pointer class' copy "
               "constructor through the auxiliary auto_array_ref object");
}
ATF_TEST_CASE_BODY(auto_array_copy_ref)
{
    using atf::auto_array;

    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    {
        auto_array< test_array > t1(new test_array[10]);
        ATF_REQUIRE_EQ(test_array::m_nblocks, 1);

        {
            auto_array< test_array > t2 = test_array::do_copy(t1);
            ATF_REQUIRE_EQ(test_array::m_nblocks, 1);
        }
        ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    }
    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
}

ATF_TEST_CASE(auto_array_get);
ATF_TEST_CASE_HEAD(auto_array_get)
{
    set_md_var("descr", "Tests the auto_array smart pointer class' get "
               "method");
}
ATF_TEST_CASE_BODY(auto_array_get)
{
    using atf::auto_array;

    test_array* ta = new test_array[10];
    auto_array< test_array > t(ta);
    ATF_REQUIRE_EQ(t.get(), ta);
}

ATF_TEST_CASE(auto_array_release);
ATF_TEST_CASE_HEAD(auto_array_release)
{
    set_md_var("descr", "Tests the auto_array smart pointer class' release "
               "method");
}
ATF_TEST_CASE_BODY(auto_array_release)
{
    using atf::auto_array;

    test_array* ta1 = new test_array[10];
    {
        auto_array< test_array > t(ta1);
        ATF_REQUIRE_EQ(test_array::m_nblocks, 1);
        test_array* ta2 = t.release();
        ATF_REQUIRE_EQ(ta2, ta1);
        ATF_REQUIRE_EQ(test_array::m_nblocks, 1);
    }
    ATF_REQUIRE_EQ(test_array::m_nblocks, 1);
    delete [] ta1;
}

ATF_TEST_CASE(auto_array_reset);
ATF_TEST_CASE_HEAD(auto_array_reset)
{
    set_md_var("descr", "Tests the auto_array smart pointer class' reset "
               "method");
}
ATF_TEST_CASE_BODY(auto_array_reset)
{
    using atf::auto_array;

    test_array* ta1 = new test_array[10];
    test_array* ta2 = new test_array[10];
    ATF_REQUIRE_EQ(test_array::m_nblocks, 2);

    {
        auto_array< test_array > t(ta1);
        ATF_REQUIRE_EQ(test_array::m_nblocks, 2);
        t.reset(ta2);
        ATF_REQUIRE_EQ(test_array::m_nblocks, 1);
        t.reset();
        ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    }
    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
}

ATF_TEST_CASE(auto_array_assign);
ATF_TEST_CASE_HEAD(auto_array_assign)
{
    set_md_var("descr", "Tests the auto_array smart pointer class' "
               "assignment operator");
}
ATF_TEST_CASE_BODY(auto_array_assign)
{
    using atf::auto_array;

    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    {
        auto_array< test_array > t1(new test_array[10]);
        ATF_REQUIRE_EQ(test_array::m_nblocks, 1);

        {
            auto_array< test_array > t2;
            t2 = t1;
            ATF_REQUIRE_EQ(test_array::m_nblocks, 1);
        }
        ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    }
    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
}

ATF_TEST_CASE(auto_array_assign_ref);
ATF_TEST_CASE_HEAD(auto_array_assign_ref)
{
    set_md_var("descr", "Tests the auto_array smart pointer class' "
               "assignment operator through the auxiliary auto_array_ref "
               "object");
}
ATF_TEST_CASE_BODY(auto_array_assign_ref)
{
    using atf::auto_array;

    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    {
        auto_array< test_array > t1(new test_array[10]);
        ATF_REQUIRE_EQ(test_array::m_nblocks, 1);

        {
            auto_array< test_array > t2;
            t2 = test_array::do_copy(t1);
            ATF_REQUIRE_EQ(test_array::m_nblocks, 1);
        }
        ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
    }
    ATF_REQUIRE_EQ(test_array::m_nblocks, 0);
}

ATF_TEST_CASE(auto_array_access);
ATF_TEST_CASE_HEAD(auto_array_access)
{
    set_md_var("descr", "Tests the auto_array smart pointer class' access "
               "operator");
}
ATF_TEST_CASE_BODY(auto_array_access)
{
    using atf::auto_array;

    auto_array< test_array > t(new test_array[10]);

    for (int i = 0; i < 10; i++)
        t[i].m_value = i * 2;

    for (int i = 0; i < 10; i++)
        ATF_REQUIRE_EQ(t[i].m_value, i * 2);
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add the test for the "auto_array" class.
    ATF_ADD_TEST_CASE(tcs, auto_array_scope);
    ATF_ADD_TEST_CASE(tcs, auto_array_copy);
    ATF_ADD_TEST_CASE(tcs, auto_array_copy_ref);
    ATF_ADD_TEST_CASE(tcs, auto_array_get);
    ATF_ADD_TEST_CASE(tcs, auto_array_release);
    ATF_ADD_TEST_CASE(tcs, auto_array_reset);
    ATF_ADD_TEST_CASE(tcs, auto_array_assign);
    ATF_ADD_TEST_CASE(tcs, auto_array_assign_ref);
    ATF_ADD_TEST_CASE(tcs, auto_array_access);
}
