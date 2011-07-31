/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * @file pagesize.h
 */

#ifndef _HV_PAGESIZE_H
#define _HV_PAGESIZE_H

/** The log2 of the size of small pages, in bytes. This value should
 * be verified at runtime by calling hv_sysconf(HV_SYSCONF_PAGE_SIZE_SMALL).
 */
#define HV_LOG2_PAGE_SIZE_SMALL 16

/** The log2 of the size of large pages, in bytes. This value should be
 * verified at runtime by calling hv_sysconf(HV_SYSCONF_PAGE_SIZE_LARGE).
 */
#define HV_LOG2_PAGE_SIZE_LARGE 24

#endif /* _HV_PAGESIZE_H */
