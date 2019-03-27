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

#include "atf-c++/detail/text.hpp"

#include <cstring>
#include <set>
#include <vector>

#include <atf-c++.hpp>

// ------------------------------------------------------------------------
// Test cases for the free functions.
// ------------------------------------------------------------------------

ATF_TEST_CASE(duplicate);
ATF_TEST_CASE_HEAD(duplicate)
{
    set_md_var("descr", "Tests the duplicate function");
}
ATF_TEST_CASE_BODY(duplicate)
{
    using atf::text::duplicate;

    const char* orig = "foo";

    char* copy = duplicate(orig);
    ATF_REQUIRE_EQ(std::strlen(copy), 3);
    ATF_REQUIRE(std::strcmp(copy, "foo") == 0);

    std::strcpy(copy, "bar");
    ATF_REQUIRE(std::strcmp(copy, "bar") == 0);
    ATF_REQUIRE(std::strcmp(orig, "foo") == 0);
}

ATF_TEST_CASE(join);
ATF_TEST_CASE_HEAD(join)
{
    set_md_var("descr", "Tests the join function");
}
ATF_TEST_CASE_BODY(join)
{
    using atf::text::join;

    // First set of tests using a non-sorted collection, std::vector.
    {
        std::vector< std::string > words;
        std::string str;

        words.clear();
        str = join(words, ",");
        ATF_REQUIRE_EQ(str, "");

        words.clear();
        words.push_back("");
        str = join(words, ",");
        ATF_REQUIRE_EQ(str, "");

        words.clear();
        words.push_back("");
        words.push_back("");
        str = join(words, ",");
        ATF_REQUIRE_EQ(str, ",");

        words.clear();
        words.push_back("foo");
        words.push_back("");
        words.push_back("baz");
        str = join(words, ",");
        ATF_REQUIRE_EQ(str, "foo,,baz");

        words.clear();
        words.push_back("foo");
        words.push_back("bar");
        words.push_back("baz");
        str = join(words, ",");
        ATF_REQUIRE_EQ(str, "foo,bar,baz");
    }

    // Second set of tests using a sorted collection, std::set.
    {
        std::set< std::string > words;
        std::string str;

        words.clear();
        str = join(words, ",");
        ATF_REQUIRE_EQ(str, "");

        words.clear();
        words.insert("");
        str = join(words, ",");
        ATF_REQUIRE_EQ(str, "");

        words.clear();
        words.insert("foo");
        words.insert("");
        words.insert("baz");
        str = join(words, ",");
        ATF_REQUIRE_EQ(str, ",baz,foo");

        words.clear();
        words.insert("foo");
        words.insert("bar");
        words.insert("baz");
        str = join(words, ",");
        ATF_REQUIRE_EQ(str, "bar,baz,foo");
    }
}

ATF_TEST_CASE(match);
ATF_TEST_CASE_HEAD(match)
{
    set_md_var("descr", "Tests the match function");
}
ATF_TEST_CASE_BODY(match)
{
    using atf::text::match;

    ATF_REQUIRE_THROW(std::runtime_error, match("", "["));

    ATF_REQUIRE(match("", ""));
    ATF_REQUIRE(!match("foo", ""));

    ATF_REQUIRE(match("", ".*"));
    ATF_REQUIRE(match("", "[a-z]*"));

    ATF_REQUIRE(match("hello", "hello"));
    ATF_REQUIRE(match("hello", "[a-z]+"));
    ATF_REQUIRE(match("hello", "^[a-z]+$"));

    ATF_REQUIRE(!match("hello", "helooo"));
    ATF_REQUIRE(!match("hello", "[a-z]+5"));
    ATF_REQUIRE(!match("hello", "^ [a-z]+$"));
}

ATF_TEST_CASE(split);
ATF_TEST_CASE_HEAD(split)
{
    set_md_var("descr", "Tests the split function");
}
ATF_TEST_CASE_BODY(split)
{
    using atf::text::split;

    std::vector< std::string > words;

    words = split("", " ");
    ATF_REQUIRE_EQ(words.size(), 0);

    words = split(" ", " ");
    ATF_REQUIRE_EQ(words.size(), 0);

    words = split("    ", " ");
    ATF_REQUIRE_EQ(words.size(), 0);

    words = split("a b", " ");
    ATF_REQUIRE_EQ(words.size(), 2);
    ATF_REQUIRE_EQ(words[0], "a");
    ATF_REQUIRE_EQ(words[1], "b");

    words = split("a b c d", " ");
    ATF_REQUIRE_EQ(words.size(), 4);
    ATF_REQUIRE_EQ(words[0], "a");
    ATF_REQUIRE_EQ(words[1], "b");
    ATF_REQUIRE_EQ(words[2], "c");
    ATF_REQUIRE_EQ(words[3], "d");

    words = split("foo bar", " ");
    ATF_REQUIRE_EQ(words.size(), 2);
    ATF_REQUIRE_EQ(words[0], "foo");
    ATF_REQUIRE_EQ(words[1], "bar");

    words = split("foo bar baz foobar", " ");
    ATF_REQUIRE_EQ(words.size(), 4);
    ATF_REQUIRE_EQ(words[0], "foo");
    ATF_REQUIRE_EQ(words[1], "bar");
    ATF_REQUIRE_EQ(words[2], "baz");
    ATF_REQUIRE_EQ(words[3], "foobar");

    words = split(" foo bar", " ");
    ATF_REQUIRE_EQ(words.size(), 2);
    ATF_REQUIRE_EQ(words[0], "foo");
    ATF_REQUIRE_EQ(words[1], "bar");

    words = split("foo  bar", " ");
    ATF_REQUIRE_EQ(words.size(), 2);
    ATF_REQUIRE_EQ(words[0], "foo");
    ATF_REQUIRE_EQ(words[1], "bar");

    words = split("foo bar ", " ");
    ATF_REQUIRE_EQ(words.size(), 2);
    ATF_REQUIRE_EQ(words[0], "foo");
    ATF_REQUIRE_EQ(words[1], "bar");

    words = split("  foo  bar  ", " ");
    ATF_REQUIRE_EQ(words.size(), 2);
    ATF_REQUIRE_EQ(words[0], "foo");
    ATF_REQUIRE_EQ(words[1], "bar");
}

ATF_TEST_CASE(split_delims);
ATF_TEST_CASE_HEAD(split_delims)
{
    set_md_var("descr", "Tests the split function using different delimiters");
}
ATF_TEST_CASE_BODY(split_delims)
{
    using atf::text::split;

    std::vector< std::string > words;

    words = split("", "/");
    ATF_REQUIRE_EQ(words.size(), 0);

    words = split(" ", "/");
    ATF_REQUIRE_EQ(words.size(), 1);
    ATF_REQUIRE_EQ(words[0], " ");

    words = split("    ", "/");
    ATF_REQUIRE_EQ(words.size(), 1);
    ATF_REQUIRE_EQ(words[0], "    ");

    words = split("a/b", "/");
    ATF_REQUIRE_EQ(words.size(), 2);
    ATF_REQUIRE_EQ(words[0], "a");
    ATF_REQUIRE_EQ(words[1], "b");

    words = split("aLONGDELIMbcdLONGDELIMef", "LONGDELIM");
    ATF_REQUIRE_EQ(words.size(), 3);
    ATF_REQUIRE_EQ(words[0], "a");
    ATF_REQUIRE_EQ(words[1], "bcd");
    ATF_REQUIRE_EQ(words[2], "ef");
}

ATF_TEST_CASE(trim);
ATF_TEST_CASE_HEAD(trim)
{
    set_md_var("descr", "Tests the trim function");
}
ATF_TEST_CASE_BODY(trim)
{
    using atf::text::trim;

    ATF_REQUIRE_EQ(trim(""), "");
    ATF_REQUIRE_EQ(trim(" "), "");
    ATF_REQUIRE_EQ(trim("\t"), "");

    ATF_REQUIRE_EQ(trim(" foo"), "foo");
    ATF_REQUIRE_EQ(trim("\t foo"), "foo");
    ATF_REQUIRE_EQ(trim(" \tfoo"), "foo");
    ATF_REQUIRE_EQ(trim("foo\t "), "foo");
    ATF_REQUIRE_EQ(trim("foo \t"), "foo");

    ATF_REQUIRE_EQ(trim("foo bar"), "foo bar");
    ATF_REQUIRE_EQ(trim("\t foo bar"), "foo bar");
    ATF_REQUIRE_EQ(trim(" \tfoo bar"), "foo bar");
    ATF_REQUIRE_EQ(trim("foo bar\t "), "foo bar");
    ATF_REQUIRE_EQ(trim("foo bar \t"), "foo bar");
}

ATF_TEST_CASE(to_bool);
ATF_TEST_CASE_HEAD(to_bool)
{
    set_md_var("descr", "Tests the to_string function");
}
ATF_TEST_CASE_BODY(to_bool)
{
    using atf::text::to_bool;

    ATF_REQUIRE(to_bool("true"));
    ATF_REQUIRE(to_bool("TRUE"));
    ATF_REQUIRE(to_bool("yes"));
    ATF_REQUIRE(to_bool("YES"));

    ATF_REQUIRE(!to_bool("false"));
    ATF_REQUIRE(!to_bool("FALSE"));
    ATF_REQUIRE(!to_bool("no"));
    ATF_REQUIRE(!to_bool("NO"));

    ATF_REQUIRE_THROW(std::runtime_error, to_bool(""));
    ATF_REQUIRE_THROW(std::runtime_error, to_bool("tru"));
    ATF_REQUIRE_THROW(std::runtime_error, to_bool("true2"));
    ATF_REQUIRE_THROW(std::runtime_error, to_bool("fals"));
    ATF_REQUIRE_THROW(std::runtime_error, to_bool("false2"));
}

ATF_TEST_CASE(to_bytes);
ATF_TEST_CASE_HEAD(to_bytes)
{
    set_md_var("descr", "Tests the to_bytes function");
}
ATF_TEST_CASE_BODY(to_bytes)
{
    using atf::text::to_bytes;

    ATF_REQUIRE_EQ(0, to_bytes("0"));
    ATF_REQUIRE_EQ(12345, to_bytes("12345"));
    ATF_REQUIRE_EQ(2 * 1024, to_bytes("2k"));
    ATF_REQUIRE_EQ(4 * 1024 * 1024, to_bytes("4m"));
    ATF_REQUIRE_EQ(int64_t(8) * 1024 * 1024 * 1024, to_bytes("8g"));
    ATF_REQUIRE_EQ(int64_t(16) * 1024 * 1024 * 1024 * 1024, to_bytes("16t"));

    ATF_REQUIRE_THROW_RE(std::runtime_error, "Empty", to_bytes(""));
    ATF_REQUIRE_THROW_RE(std::runtime_error, "Unknown size unit 'd'",
                         to_bytes("12d"));
    ATF_REQUIRE_THROW(std::runtime_error, to_bytes(" "));
    ATF_REQUIRE_THROW(std::runtime_error, to_bytes(" k"));
}

ATF_TEST_CASE(to_string);
ATF_TEST_CASE_HEAD(to_string)
{
    set_md_var("descr", "Tests the to_string function");
}
ATF_TEST_CASE_BODY(to_string)
{
    using atf::text::to_string;

    ATF_REQUIRE_EQ(to_string('a'), "a");
    ATF_REQUIRE_EQ(to_string("a"), "a");
    ATF_REQUIRE_EQ(to_string(5), "5");
}

ATF_TEST_CASE(to_type);
ATF_TEST_CASE_HEAD(to_type)
{
    set_md_var("descr", "Tests the to_type function");
}
ATF_TEST_CASE_BODY(to_type)
{
    using atf::text::to_type;

    ATF_REQUIRE_EQ(to_type< int >("0"), 0);
    ATF_REQUIRE_EQ(to_type< int >("1234"), 1234);
    ATF_REQUIRE_THROW(std::runtime_error, to_type< int >("   "));
    ATF_REQUIRE_THROW(std::runtime_error, to_type< int >("0 a"));
    ATF_REQUIRE_THROW(std::runtime_error, to_type< int >("a"));

    ATF_REQUIRE_EQ(to_type< float >("0.5"), 0.5);
    ATF_REQUIRE_EQ(to_type< float >("1234.5"), 1234.5);
    ATF_REQUIRE_THROW(std::runtime_error, to_type< float >("0.5 a"));
    ATF_REQUIRE_THROW(std::runtime_error, to_type< float >("a"));

    ATF_REQUIRE_EQ(to_type< std::string >("a"), "a");
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add the test cases for the free functions.
    ATF_ADD_TEST_CASE(tcs, duplicate);
    ATF_ADD_TEST_CASE(tcs, join);
    ATF_ADD_TEST_CASE(tcs, match);
    ATF_ADD_TEST_CASE(tcs, split);
    ATF_ADD_TEST_CASE(tcs, split_delims);
    ATF_ADD_TEST_CASE(tcs, trim);
    ATF_ADD_TEST_CASE(tcs, to_bool);
    ATF_ADD_TEST_CASE(tcs, to_bytes);
    ATF_ADD_TEST_CASE(tcs, to_string);
    ATF_ADD_TEST_CASE(tcs, to_type);
}
