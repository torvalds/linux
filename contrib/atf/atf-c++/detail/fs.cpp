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

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

extern "C" {
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <libgen.h>
#include <unistd.h>
}

#include <cerrno>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "atf-c/error.h"
}

#include "atf-c++/detail/env.hpp"
#include "atf-c++/detail/exceptions.hpp"
#include "atf-c++/detail/process.hpp"
#include "atf-c++/detail/sanity.hpp"
#include "atf-c++/detail/text.hpp"
#include "atf-c++/utils.hpp"

namespace impl = atf::fs;
#define IMPL_NAME "atf::fs"

// ------------------------------------------------------------------------
// Auxiliary functions.
// ------------------------------------------------------------------------

static bool safe_access(const impl::path&, int, int);

//!
//! \brief A controlled version of access(2).
//!
//! This function reimplements the standard access(2) system call to
//! safely control its exit status and raise an exception in case of
//! failure.
//!
static
bool
safe_access(const impl::path& p, int mode, int experr)
{
    bool ok;

    atf_error_t err = atf_fs_eaccess(p.c_path(), mode);
    if (atf_is_error(err)) {
        if (atf_error_is(err, "libc")) {
            if (atf_libc_error_code(err) == experr) {
                atf_error_free(err);
                ok = false;
            } else {
                atf::throw_atf_error(err);
                // XXX Silence warning; maybe throw_atf_error should be
                // an exception and not a function.
                ok = false;
            }
        } else {
            atf::throw_atf_error(err);
            // XXX Silence warning; maybe throw_atf_error should be
            // an exception and not a function.
            ok = false;
        }
    } else
        ok = true;

    return ok;
}

// ------------------------------------------------------------------------
// The "path" class.
// ------------------------------------------------------------------------

impl::path::path(const std::string& s)
{
    atf_error_t err = atf_fs_path_init_fmt(&m_path, "%s", s.c_str());
    if (atf_is_error(err))
        throw_atf_error(err);
}

impl::path::path(const path& p)
{
    atf_error_t err = atf_fs_path_copy(&m_path, &p.m_path);
    if (atf_is_error(err))
        throw_atf_error(err);
}

impl::path::path(const atf_fs_path_t *p)
{
    atf_error_t err = atf_fs_path_copy(&m_path, p);
    if (atf_is_error(err))
        throw_atf_error(err);
}

impl::path::~path(void)
{
    atf_fs_path_fini(&m_path);
}

const char*
impl::path::c_str(void)
    const
{
    return atf_fs_path_cstring(&m_path);
}

const atf_fs_path_t*
impl::path::c_path(void)
    const
{
    return &m_path;
}

std::string
impl::path::str(void)
    const
{
    return c_str();
}

bool
impl::path::is_absolute(void)
    const
{
    return atf_fs_path_is_absolute(&m_path);
}

bool
impl::path::is_root(void)
    const
{
    return atf_fs_path_is_root(&m_path);
}

impl::path
impl::path::branch_path(void)
    const
{
    atf_fs_path_t bp;
    atf_error_t err;

    err = atf_fs_path_branch_path(&m_path, &bp);
    if (atf_is_error(err))
        throw_atf_error(err);

    path p(atf_fs_path_cstring(&bp));
    atf_fs_path_fini(&bp);
    return p;
}

std::string
impl::path::leaf_name(void)
    const
{
    atf_dynstr_t ln;
    atf_error_t err;

    err = atf_fs_path_leaf_name(&m_path, &ln);
    if (atf_is_error(err))
        throw_atf_error(err);

    std::string s(atf_dynstr_cstring(&ln));
    atf_dynstr_fini(&ln);
    return s;
}

impl::path
impl::path::to_absolute(void)
    const
{
    atf_fs_path_t pa;

    atf_error_t err = atf_fs_path_to_absolute(&m_path, &pa);
    if (atf_is_error(err))
        throw_atf_error(err);

    path p(atf_fs_path_cstring(&pa));
    atf_fs_path_fini(&pa);
    return p;
}

impl::path&
impl::path::operator=(const path& p)
{
    atf_fs_path_t tmp;

    atf_error_t err = atf_fs_path_init_fmt(&tmp, "%s", p.c_str());
    if (atf_is_error(err))
        throw_atf_error(err);
    else {
        atf_fs_path_fini(&m_path);
        m_path = tmp;
    }

    return *this;
}

bool
impl::path::operator==(const path& p)
    const
{
    return atf_equal_fs_path_fs_path(&m_path, &p.m_path);
}

bool
impl::path::operator!=(const path& p)
    const
{
    return !atf_equal_fs_path_fs_path(&m_path, &p.m_path);
}

impl::path
impl::path::operator/(const std::string& p)
    const
{
    path p2 = *this;

    atf_error_t err = atf_fs_path_append_fmt(&p2.m_path, "%s", p.c_str());
    if (atf_is_error(err))
        throw_atf_error(err);

    return p2;
}

impl::path
impl::path::operator/(const path& p)
    const
{
    path p2 = *this;

    atf_error_t err = atf_fs_path_append_fmt(&p2.m_path, "%s",
                                             atf_fs_path_cstring(&p.m_path));
    if (atf_is_error(err))
        throw_atf_error(err);

    return p2;
}

bool
impl::path::operator<(const path& p)
    const
{
    const char *s1 = atf_fs_path_cstring(&m_path);
    const char *s2 = atf_fs_path_cstring(&p.m_path);
    return std::strcmp(s1, s2) < 0;
}

// ------------------------------------------------------------------------
// The "file_info" class.
// ------------------------------------------------------------------------

const int impl::file_info::blk_type = atf_fs_stat_blk_type;
const int impl::file_info::chr_type = atf_fs_stat_chr_type;
const int impl::file_info::dir_type = atf_fs_stat_dir_type;
const int impl::file_info::fifo_type = atf_fs_stat_fifo_type;
const int impl::file_info::lnk_type = atf_fs_stat_lnk_type;
const int impl::file_info::reg_type = atf_fs_stat_reg_type;
const int impl::file_info::sock_type = atf_fs_stat_sock_type;
const int impl::file_info::wht_type = atf_fs_stat_wht_type;

impl::file_info::file_info(const path& p)
{
    atf_error_t err;

    err = atf_fs_stat_init(&m_stat, p.c_path());
    if (atf_is_error(err))
        throw_atf_error(err);
}

impl::file_info::file_info(const file_info& fi)
{
    atf_fs_stat_copy(&m_stat, &fi.m_stat);
}

impl::file_info::~file_info(void)
{
    atf_fs_stat_fini(&m_stat);
}

dev_t
impl::file_info::get_device(void)
    const
{
    return atf_fs_stat_get_device(&m_stat);
}

ino_t
impl::file_info::get_inode(void)
    const
{
    return atf_fs_stat_get_inode(&m_stat);
}

mode_t
impl::file_info::get_mode(void)
    const
{
    return atf_fs_stat_get_mode(&m_stat);
}

off_t
impl::file_info::get_size(void)
    const
{
    return atf_fs_stat_get_size(&m_stat);
}

int
impl::file_info::get_type(void)
    const
{
    return atf_fs_stat_get_type(&m_stat);
}

bool
impl::file_info::is_owner_readable(void)
    const
{
    return atf_fs_stat_is_owner_readable(&m_stat);
}

bool
impl::file_info::is_owner_writable(void)
    const
{
    return atf_fs_stat_is_owner_writable(&m_stat);
}

bool
impl::file_info::is_owner_executable(void)
    const
{
    return atf_fs_stat_is_owner_executable(&m_stat);
}

bool
impl::file_info::is_group_readable(void)
    const
{
    return atf_fs_stat_is_group_readable(&m_stat);
}

bool
impl::file_info::is_group_writable(void)
    const
{
    return atf_fs_stat_is_group_writable(&m_stat);
}

bool
impl::file_info::is_group_executable(void)
    const
{
    return atf_fs_stat_is_group_executable(&m_stat);
}

bool
impl::file_info::is_other_readable(void)
    const
{
    return atf_fs_stat_is_other_readable(&m_stat);
}

bool
impl::file_info::is_other_writable(void)
    const
{
    return atf_fs_stat_is_other_writable(&m_stat);
}

bool
impl::file_info::is_other_executable(void)
    const
{
    return atf_fs_stat_is_other_executable(&m_stat);
}

// ------------------------------------------------------------------------
// The "directory" class.
// ------------------------------------------------------------------------

impl::directory::directory(const path& p)
{
    DIR* dp = ::opendir(p.c_str());
    if (dp == NULL)
        throw system_error(IMPL_NAME "::directory::directory(" +
                           p.str() + ")", "opendir(3) failed", errno);

    struct dirent* dep;
    while ((dep = ::readdir(dp)) != NULL) {
        path entryp = p / dep->d_name;
        insert(value_type(dep->d_name, file_info(entryp)));
    }

    if (::closedir(dp) == -1)
        throw system_error(IMPL_NAME "::directory::directory(" +
                           p.str() + ")", "closedir(3) failed", errno);
}

std::set< std::string >
impl::directory::names(void)
    const
{
    std::set< std::string > ns;

    for (const_iterator iter = begin(); iter != end(); iter++)
        ns.insert((*iter).first);

    return ns;
}

// ------------------------------------------------------------------------
// Free functions.
// ------------------------------------------------------------------------

bool
impl::exists(const path& p)
{
    atf_error_t err;
    bool b;

    err = atf_fs_exists(p.c_path(), &b);
    if (atf_is_error(err))
        throw_atf_error(err);

    return b;
}

bool
impl::have_prog_in_path(const std::string& prog)
{
    PRE(prog.find('/') == std::string::npos);

    // Do not bother to provide a default value for PATH.  If it is not
    // there something is broken in the user's environment.
    if (!atf::env::has("PATH"))
        throw std::runtime_error("PATH not defined in the environment");
    std::vector< std::string > dirs =
        atf::text::split(atf::env::get("PATH"), ":");

    bool found = false;
    for (std::vector< std::string >::const_iterator iter = dirs.begin();
         !found && iter != dirs.end(); iter++) {
        const path& dir = path(*iter);

        if (is_executable(dir / prog))
            found = true;
    }
    return found;
}

bool
impl::is_executable(const path& p)
{
    if (!exists(p))
        return false;
    return safe_access(p, atf_fs_access_x, EACCES);
}

void
impl::remove(const path& p)
{
    if (file_info(p).get_type() == file_info::dir_type)
        throw atf::system_error(IMPL_NAME "::remove(" + p.str() + ")",
                                "Is a directory",
                                EPERM);
    if (::unlink(p.c_str()) == -1)
        throw atf::system_error(IMPL_NAME "::remove(" + p.str() + ")",
                                "unlink(" + p.str() + ") failed",
                                errno);
}

void
impl::rmdir(const path& p)
{
    atf_error_t err = atf_fs_rmdir(p.c_path());
    if (atf_is_error(err))
        throw_atf_error(err);
}
