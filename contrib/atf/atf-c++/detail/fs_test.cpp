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

#include "atf-c++/detail/fs.hpp"

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
}

#include <fstream>
#include <cerrno>
#include <cstdio>

#include <atf-c++.hpp>

#include "atf-c++/detail/exceptions.hpp"

// ------------------------------------------------------------------------
// Auxiliary functions.
// ------------------------------------------------------------------------

static
void
create_files(void)
{
    ::mkdir("files", 0755);
    ::mkdir("files/dir", 0755);

    std::ofstream os("files/reg");
    os.close();

    // TODO: Should create all other file types (blk, chr, fifo, lnk, sock)
    // and test for them... but the underlying file system may not support
    // most of these.  Specially as we are working on /tmp, which can be
    // mounted with flags such as "nodev".  See how to deal with this
    // situation.
}

// ------------------------------------------------------------------------
// Test cases for the "path" class.
// ------------------------------------------------------------------------

ATF_TEST_CASE(path_normalize);
ATF_TEST_CASE_HEAD(path_normalize)
{
    set_md_var("descr", "Tests the path's normalization");
}
ATF_TEST_CASE_BODY(path_normalize)
{
    using atf::fs::path;

    ATF_REQUIRE_EQ(path(".").str(), ".");
    ATF_REQUIRE_EQ(path("..").str(), "..");

    ATF_REQUIRE_EQ(path("foo").str(), "foo");
    ATF_REQUIRE_EQ(path("foo/bar").str(), "foo/bar");
    ATF_REQUIRE_EQ(path("foo/bar/").str(), "foo/bar");

    ATF_REQUIRE_EQ(path("/foo").str(), "/foo");
    ATF_REQUIRE_EQ(path("/foo/bar").str(), "/foo/bar");
    ATF_REQUIRE_EQ(path("/foo/bar/").str(), "/foo/bar");

    ATF_REQUIRE_EQ(path("///foo").str(), "/foo");
    ATF_REQUIRE_EQ(path("///foo///bar").str(), "/foo/bar");
    ATF_REQUIRE_EQ(path("///foo///bar///").str(), "/foo/bar");
}

ATF_TEST_CASE(path_is_absolute);
ATF_TEST_CASE_HEAD(path_is_absolute)
{
    set_md_var("descr", "Tests the path::is_absolute function");
}
ATF_TEST_CASE_BODY(path_is_absolute)
{
    using atf::fs::path;

    ATF_REQUIRE( path("/").is_absolute());
    ATF_REQUIRE( path("////").is_absolute());
    ATF_REQUIRE( path("////a").is_absolute());
    ATF_REQUIRE( path("//a//").is_absolute());
    ATF_REQUIRE(!path("a////").is_absolute());
    ATF_REQUIRE(!path("../foo").is_absolute());
}

ATF_TEST_CASE(path_is_root);
ATF_TEST_CASE_HEAD(path_is_root)
{
    set_md_var("descr", "Tests the path::is_root function");
}
ATF_TEST_CASE_BODY(path_is_root)
{
    using atf::fs::path;

    ATF_REQUIRE( path("/").is_root());
    ATF_REQUIRE( path("////").is_root());
    ATF_REQUIRE(!path("////a").is_root());
    ATF_REQUIRE(!path("//a//").is_root());
    ATF_REQUIRE(!path("a////").is_root());
    ATF_REQUIRE(!path("../foo").is_root());
}

ATF_TEST_CASE(path_branch_path);
ATF_TEST_CASE_HEAD(path_branch_path)
{
    set_md_var("descr", "Tests the path::branch_path function");
}
ATF_TEST_CASE_BODY(path_branch_path)
{
    using atf::fs::path;

    ATF_REQUIRE_EQ(path(".").branch_path().str(), ".");
    ATF_REQUIRE_EQ(path("foo").branch_path().str(), ".");
    ATF_REQUIRE_EQ(path("foo/bar").branch_path().str(), "foo");
    ATF_REQUIRE_EQ(path("/foo").branch_path().str(), "/");
    ATF_REQUIRE_EQ(path("/foo/bar").branch_path().str(), "/foo");
}

ATF_TEST_CASE(path_leaf_name);
ATF_TEST_CASE_HEAD(path_leaf_name)
{
    set_md_var("descr", "Tests the path::leaf_name function");
}
ATF_TEST_CASE_BODY(path_leaf_name)
{
    using atf::fs::path;

    ATF_REQUIRE_EQ(path(".").leaf_name(), ".");
    ATF_REQUIRE_EQ(path("foo").leaf_name(), "foo");
    ATF_REQUIRE_EQ(path("foo/bar").leaf_name(), "bar");
    ATF_REQUIRE_EQ(path("/foo").leaf_name(), "foo");
    ATF_REQUIRE_EQ(path("/foo/bar").leaf_name(), "bar");
}

ATF_TEST_CASE(path_compare_equal);
ATF_TEST_CASE_HEAD(path_compare_equal)
{
    set_md_var("descr", "Tests the comparison for equality between paths");
}
ATF_TEST_CASE_BODY(path_compare_equal)
{
    using atf::fs::path;

    ATF_REQUIRE(path("/") == path("///"));
    ATF_REQUIRE(path("/a") == path("///a"));
    ATF_REQUIRE(path("/a") == path("///a///"));

    ATF_REQUIRE(path("a/b/c") == path("a//b//c"));
    ATF_REQUIRE(path("a/b/c") == path("a//b//c///"));
}

ATF_TEST_CASE(path_compare_different);
ATF_TEST_CASE_HEAD(path_compare_different)
{
    set_md_var("descr", "Tests the comparison for difference between paths");
}
ATF_TEST_CASE_BODY(path_compare_different)
{
    using atf::fs::path;

    ATF_REQUIRE(path("/") != path("//a/"));
    ATF_REQUIRE(path("/a") != path("a///"));

    ATF_REQUIRE(path("a/b/c") != path("a/b"));
    ATF_REQUIRE(path("a/b/c") != path("a//b"));
    ATF_REQUIRE(path("a/b/c") != path("/a/b/c"));
    ATF_REQUIRE(path("a/b/c") != path("/a//b//c"));
}

ATF_TEST_CASE(path_concat);
ATF_TEST_CASE_HEAD(path_concat)
{
    set_md_var("descr", "Tests the concatenation of multiple paths");
}
ATF_TEST_CASE_BODY(path_concat)
{
    using atf::fs::path;

    ATF_REQUIRE_EQ((path("foo") / "bar").str(), "foo/bar");
    ATF_REQUIRE_EQ((path("foo/") / "/bar").str(), "foo/bar");
    ATF_REQUIRE_EQ((path("foo/") / "/bar/baz").str(), "foo/bar/baz");
    ATF_REQUIRE_EQ((path("foo/") / "///bar///baz").str(), "foo/bar/baz");
}

ATF_TEST_CASE(path_to_absolute);
ATF_TEST_CASE_HEAD(path_to_absolute)
{
    set_md_var("descr", "Tests the conversion of a relative path to an "
               "absolute one");
}
ATF_TEST_CASE_BODY(path_to_absolute)
{
    using atf::fs::file_info;
    using atf::fs::path;

    create_files();

    {
        const path p(".");
        path pa = p.to_absolute();
        ATF_REQUIRE(pa.is_absolute());

        file_info fi(p);
        file_info fia(pa);
        ATF_REQUIRE_EQ(fi.get_device(), fia.get_device());
        ATF_REQUIRE_EQ(fi.get_inode(), fia.get_inode());
    }

    {
        const path p("files/reg");
        path pa = p.to_absolute();
        ATF_REQUIRE(pa.is_absolute());

        file_info fi(p);
        file_info fia(pa);
        ATF_REQUIRE_EQ(fi.get_device(), fia.get_device());
        ATF_REQUIRE_EQ(fi.get_inode(), fia.get_inode());
    }
}

ATF_TEST_CASE(path_op_less);
ATF_TEST_CASE_HEAD(path_op_less)
{
    set_md_var("descr", "Tests that the path's less-than operator works");
}
ATF_TEST_CASE_BODY(path_op_less)
{
    using atf::fs::path;

    create_files();

    ATF_REQUIRE(!(path("aaa") < path("aaa")));

    ATF_REQUIRE(  path("aab") < path("abc"));
    ATF_REQUIRE(!(path("abc") < path("aab")));
}

// ------------------------------------------------------------------------
// Test cases for the "directory" class.
// ------------------------------------------------------------------------

ATF_TEST_CASE(directory_read);
ATF_TEST_CASE_HEAD(directory_read)
{
    set_md_var("descr", "Tests the directory class creation, which reads "
               "the contents of a directory");
}
ATF_TEST_CASE_BODY(directory_read)
{
    using atf::fs::directory;
    using atf::fs::path;

    create_files();

    directory d(path("files"));
    ATF_REQUIRE_EQ(d.size(), 4);
    ATF_REQUIRE(d.find(".") != d.end());
    ATF_REQUIRE(d.find("..") != d.end());
    ATF_REQUIRE(d.find("dir") != d.end());
    ATF_REQUIRE(d.find("reg") != d.end());
}

ATF_TEST_CASE(directory_file_info);
ATF_TEST_CASE_HEAD(directory_file_info)
{
    set_md_var("descr", "Tests that the file_info objects attached to the "
               "directory are valid");
}
ATF_TEST_CASE_BODY(directory_file_info)
{
    using atf::fs::directory;
    using atf::fs::file_info;
    using atf::fs::path;

    create_files();

    directory d(path("files"));

    {
        directory::const_iterator iter = d.find("dir");
        ATF_REQUIRE(iter != d.end());
        const file_info& fi = (*iter).second;
        ATF_REQUIRE(fi.get_type() == file_info::dir_type);
    }

    {
        directory::const_iterator iter = d.find("reg");
        ATF_REQUIRE(iter != d.end());
        const file_info& fi = (*iter).second;
        ATF_REQUIRE(fi.get_type() == file_info::reg_type);
    }
}

ATF_TEST_CASE(directory_names);
ATF_TEST_CASE_HEAD(directory_names)
{
    set_md_var("descr", "Tests the directory's names method");
}
ATF_TEST_CASE_BODY(directory_names)
{
    using atf::fs::directory;
    using atf::fs::path;

    create_files();

    directory d(path("files"));
    std::set< std::string > ns = d.names();
    ATF_REQUIRE_EQ(ns.size(), 4);
    ATF_REQUIRE(ns.find(".") != ns.end());
    ATF_REQUIRE(ns.find("..") != ns.end());
    ATF_REQUIRE(ns.find("dir") != ns.end());
    ATF_REQUIRE(ns.find("reg") != ns.end());
}

// ------------------------------------------------------------------------
// Test cases for the "file_info" class.
// ------------------------------------------------------------------------

ATF_TEST_CASE(file_info_stat);
ATF_TEST_CASE_HEAD(file_info_stat)
{
    set_md_var("descr", "Tests the file_info creation and its basic contents");
}
ATF_TEST_CASE_BODY(file_info_stat)
{
    using atf::fs::file_info;
    using atf::fs::path;

    create_files();

    {
        path p("files/dir");
        file_info fi(p);
        ATF_REQUIRE(fi.get_type() == file_info::dir_type);
    }

    {
        path p("files/reg");
        file_info fi(p);
        ATF_REQUIRE(fi.get_type() == file_info::reg_type);
    }
}

ATF_TEST_CASE(file_info_perms);
ATF_TEST_CASE_HEAD(file_info_perms)
{
    set_md_var("descr", "Tests the file_info methods to get the file's "
               "permissions");
}
ATF_TEST_CASE_BODY(file_info_perms)
{
    using atf::fs::file_info;
    using atf::fs::path;

    path p("file");

    std::ofstream os(p.c_str());
    os.close();

#define perms(ur, uw, ux, gr, gw, gx, othr, othw, othx) \
    { \
        file_info fi(p); \
        ATF_REQUIRE(fi.is_owner_readable() == ur); \
        ATF_REQUIRE(fi.is_owner_writable() == uw); \
        ATF_REQUIRE(fi.is_owner_executable() == ux); \
        ATF_REQUIRE(fi.is_group_readable() == gr); \
        ATF_REQUIRE(fi.is_group_writable() == gw); \
        ATF_REQUIRE(fi.is_group_executable() == gx); \
        ATF_REQUIRE(fi.is_other_readable() == othr); \
        ATF_REQUIRE(fi.is_other_writable() == othw); \
        ATF_REQUIRE(fi.is_other_executable() == othx); \
    }

    ::chmod(p.c_str(), 0000);
    perms(false, false, false, false, false, false, false, false, false);

    ::chmod(p.c_str(), 0001);
    perms(false, false, false, false, false, false, false, false, true);

    ::chmod(p.c_str(), 0010);
    perms(false, false, false, false, false, true, false, false, false);

    ::chmod(p.c_str(), 0100);
    perms(false, false, true, false, false, false, false, false, false);

    ::chmod(p.c_str(), 0002);
    perms(false, false, false, false, false, false, false, true, false);

    ::chmod(p.c_str(), 0020);
    perms(false, false, false, false, true, false, false, false, false);

    ::chmod(p.c_str(), 0200);
    perms(false, true, false, false, false, false, false, false, false);

    ::chmod(p.c_str(), 0004);
    perms(false, false, false, false, false, false, true, false, false);

    ::chmod(p.c_str(), 0040);
    perms(false, false, false, true, false, false, false, false, false);

    ::chmod(p.c_str(), 0400);
    perms(true, false, false, false, false, false, false, false, false);

    ::chmod(p.c_str(), 0644);
    perms(true, true, false, true, false, false, true, false, false);

    ::chmod(p.c_str(), 0755);
    perms(true, true, true, true, false, true, true, false, true);

    ::chmod(p.c_str(), 0777);
    perms(true, true, true, true, true, true, true, true, true);

#undef perms
}

// ------------------------------------------------------------------------
// Test cases for the free functions.
// ------------------------------------------------------------------------

ATF_TEST_CASE(exists);
ATF_TEST_CASE_HEAD(exists)
{
    set_md_var("descr", "Tests the exists function");
}
ATF_TEST_CASE_BODY(exists)
{
    using atf::fs::exists;
    using atf::fs::path;

    create_files();

    ATF_REQUIRE( exists(path("files")));
    ATF_REQUIRE(!exists(path("file")));
    ATF_REQUIRE(!exists(path("files2")));

    ATF_REQUIRE( exists(path("files/.")));
    ATF_REQUIRE( exists(path("files/..")));
    ATF_REQUIRE( exists(path("files/dir")));
    ATF_REQUIRE( exists(path("files/reg")));
    ATF_REQUIRE(!exists(path("files/foo")));
}

ATF_TEST_CASE(is_executable);
ATF_TEST_CASE_HEAD(is_executable)
{
    set_md_var("descr", "Tests the is_executable function");
}
ATF_TEST_CASE_BODY(is_executable)
{
    using atf::fs::is_executable;
    using atf::fs::path;

    create_files();

    ATF_REQUIRE( is_executable(path("files")));
    ATF_REQUIRE( is_executable(path("files/.")));
    ATF_REQUIRE( is_executable(path("files/..")));
    ATF_REQUIRE( is_executable(path("files/dir")));

    ATF_REQUIRE(!is_executable(path("non-existent")));

    ATF_REQUIRE(!is_executable(path("files/reg")));
    ATF_REQUIRE(::chmod("files/reg", 0755) != -1);
    ATF_REQUIRE( is_executable(path("files/reg")));
}

ATF_TEST_CASE(remove);
ATF_TEST_CASE_HEAD(remove)
{
    set_md_var("descr", "Tests the remove function");
}
ATF_TEST_CASE_BODY(remove)
{
    using atf::fs::exists;
    using atf::fs::path;
    using atf::fs::remove;

    create_files();

    ATF_REQUIRE( exists(path("files/reg")));
    remove(path("files/reg"));
    ATF_REQUIRE(!exists(path("files/reg")));

    ATF_REQUIRE( exists(path("files/dir")));
    ATF_REQUIRE_THROW(atf::system_error, remove(path("files/dir")));
    ATF_REQUIRE( exists(path("files/dir")));
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add the tests for the "path" class.
    ATF_ADD_TEST_CASE(tcs, path_normalize);
    ATF_ADD_TEST_CASE(tcs, path_is_absolute);
    ATF_ADD_TEST_CASE(tcs, path_is_root);
    ATF_ADD_TEST_CASE(tcs, path_branch_path);
    ATF_ADD_TEST_CASE(tcs, path_leaf_name);
    ATF_ADD_TEST_CASE(tcs, path_compare_equal);
    ATF_ADD_TEST_CASE(tcs, path_compare_different);
    ATF_ADD_TEST_CASE(tcs, path_concat);
    ATF_ADD_TEST_CASE(tcs, path_to_absolute);
    ATF_ADD_TEST_CASE(tcs, path_op_less);

    // Add the tests for the "file_info" class.
    ATF_ADD_TEST_CASE(tcs, file_info_stat);
    ATF_ADD_TEST_CASE(tcs, file_info_perms);

    // Add the tests for the "directory" class.
    ATF_ADD_TEST_CASE(tcs, directory_read);
    ATF_ADD_TEST_CASE(tcs, directory_names);
    ATF_ADD_TEST_CASE(tcs, directory_file_info);

    // Add the tests for the free functions.
    ATF_ADD_TEST_CASE(tcs, exists);
    ATF_ADD_TEST_CASE(tcs, is_executable);
    ATF_ADD_TEST_CASE(tcs, remove);
}
