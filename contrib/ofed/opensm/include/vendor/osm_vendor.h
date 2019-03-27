/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 * 	Include file used by OpenSM to pull in the correct vendor file.
 */

/*
  this is the generic include file which includes
  the proper vendor specific file
*/
#include <opensm/osm_config.h>

#if defined( OSM_VENDOR_INTF_TEST )
#include <vendor/osm_vendor_test.h>
#elif defined( OSM_VENDOR_INTF_UMADT )
#include <vendor/osm_vendor_umadt.h>
#elif defined( OSM_VENDOR_INTF_MTL )
/* HACK - I do not know how to prevent complib from loading kernel H files */
#undef __init
#include <vendor/osm_vendor_mlx.h>
#elif defined( OSM_VENDOR_INTF_TS )
#undef __init
#include <vendor/osm_vendor_mlx.h>
#elif defined( OSM_VENDOR_INTF_ANAFA )
#undef __init
#include <vendor/osm_vendor_mlx.h>
#elif defined( OSM_VENDOR_INTF_SIM )
#undef __init
#include <vendor/osm_vendor_mlx.h>
#elif defined( OSM_VENDOR_INTF_OPENIB )
#include <vendor/osm_vendor_ibumad.h>
#elif defined( OSM_VENDOR_INTF_AL )
#include <vendor/osm_vendor_al.h>
#else
#error No MAD Interface selected!
#error Choose an interface in osm_config.h
#endif
