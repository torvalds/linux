/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/amq/amq_xdr.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amq.h>


bool_t
xdr_time_type(XDR *xdrs, time_type *objp)
{
  if (!xdr_long(xdrs, (long *) objp)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_mount_tree(XDR *xdrs, amq_mount_tree *objp)
{

  if (!xdr_amq_string(xdrs, &objp->mt_mountinfo)) {
    return (FALSE);
  }

  if (!xdr_amq_string(xdrs, &objp->mt_directory)) {
    return (FALSE);
  }

  if (!xdr_amq_string(xdrs, &objp->mt_mountpoint)) {
    return (FALSE);
  }

  if (!xdr_amq_string(xdrs, &objp->mt_type)) {
    return (FALSE);
  }

  if (!xdr_time_type(xdrs, &objp->mt_mounttime)) {
    return (FALSE);
  }

  if (!xdr_u_short(xdrs, &objp->mt_mountuid)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mt_getattr)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mt_lookup)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mt_readdir)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mt_readlink)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mt_statfs)) {
    return (FALSE);
  }

  if (!xdr_pointer(xdrs,
		   (char **) ((voidp) &objp->mt_next),
		   sizeof(amq_mount_tree),
		   (XDRPROC_T_TYPE) xdr_amq_mount_tree)) {
    return (FALSE);
  }

  if (!xdr_pointer(xdrs,
		   (char **) ((voidp) &objp->mt_child),
		   sizeof(amq_mount_tree),
		   (XDRPROC_T_TYPE) xdr_amq_mount_tree)) {
    return (FALSE);
  }

  return (TRUE);
}


bool_t
xdr_amq_mount_tree_p(XDR *xdrs, amq_mount_tree_p *objp)
{
  if (!xdr_pointer(xdrs,
		   (char **) objp,
		   sizeof(amq_mount_tree),
		   (XDRPROC_T_TYPE) xdr_amq_mount_tree)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_mount_info(XDR *xdrs, amq_mount_info *objp)
{

  if (!xdr_amq_string(xdrs, &objp->mi_type)) {
    return (FALSE);
  }

  if (!xdr_amq_string(xdrs, &objp->mi_mountpt)) {
    return (FALSE);
  }

  if (!xdr_amq_string(xdrs, &objp->mi_mountinfo)) {
    return (FALSE);
  }

  if (!xdr_amq_string(xdrs, &objp->mi_fserver)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mi_error)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mi_refc)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mi_up)) {
    return (FALSE);
  }

  return (TRUE);
}


bool_t
xdr_amq_mount_info_list(XDR *xdrs, amq_mount_info_list *objp)
{
  if (!xdr_array(xdrs,
		 (char **) ((voidp) &objp->amq_mount_info_list_val),
		 (u_int *) &objp->amq_mount_info_list_len,
		 ~0,
		 sizeof(amq_mount_info),
		 (XDRPROC_T_TYPE) xdr_amq_mount_info)) {
    return (FALSE);
  }
  return (TRUE);
}

bool_t
xdr_amq_map_info(XDR *xdrs, amq_map_info *objp)
{
  if (!xdr_amq_string(xdrs, &objp->mi_name)) {
    return (FALSE);
  }

  if (!xdr_amq_string(xdrs, &objp->mi_wildcard)) {
    return (FALSE);
  }

  if (!xdr_time_type(xdrs, &objp->mi_modify)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mi_flags)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mi_nentries)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mi_reloads)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mi_refc)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->mi_up)) {
    return (FALSE);
  }

  return (TRUE);
}


bool_t
xdr_amq_map_info_list(XDR *xdrs, amq_map_info_list *objp)
{
  if (!xdr_array(xdrs,
		 (char **) ((voidp) &objp->amq_map_info_list_val),
		 (u_int *) &objp->amq_map_info_list_len,
		 ~0,
		 sizeof(amq_map_info),
		 (XDRPROC_T_TYPE) xdr_amq_map_info)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_mount_tree_list(XDR *xdrs, amq_mount_tree_list *objp)
{
  if (!xdr_array(xdrs,
		 (char **) ((voidp) &objp->amq_mount_tree_list_val),
		 (u_int *) &objp->amq_mount_tree_list_len,
		 ~0,
		 sizeof(amq_mount_tree_p),
		 (XDRPROC_T_TYPE) xdr_amq_mount_tree_p)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_mount_stats(XDR *xdrs, amq_mount_stats *objp)
{

  if (!xdr_int(xdrs, &objp->as_drops)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->as_stale)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->as_mok)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->as_merr)) {
    return (FALSE);
  }

  if (!xdr_int(xdrs, &objp->as_uerr)) {
    return (FALSE);
  }

  return (TRUE);
}


bool_t
xdr_amq_opt(XDR *xdrs, amq_opt *objp)
{
  if (!xdr_enum(xdrs, (enum_t *) objp)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_setopt(XDR *xdrs, amq_setopt *objp)
{

  if (!xdr_amq_opt(xdrs, &objp->as_opt)) {
    return (FALSE);
  }

  if (!xdr_amq_string(xdrs, &objp->as_str)) {
    return (FALSE);
  }

  return (TRUE);
}


bool_t
xdr_pri_free(XDRPROC_T_TYPE xdr_args, caddr_t args_ptr)
{
  XDR xdr;

  xdr.x_op = XDR_FREE;
  return ((*xdr_args) (&xdr, (caddr_t *) args_ptr));
}
