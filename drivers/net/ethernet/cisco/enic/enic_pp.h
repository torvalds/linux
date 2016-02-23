/*
 * Copyright 2011 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
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

#ifndef _ENIC_PP_H_
#define _ENIC_PP_H_

#define ENIC_PP_BY_INDEX(enic, vf, pp, err) \
	do { \
		if (enic_is_valid_pp_vf(enic, vf, err)) \
			pp = (vf == PORT_SELF_VF) ? enic->pp : enic->pp + vf; \
		else \
			pp = NULL; \
	} while (0)

int enic_process_set_pp_request(struct enic *enic, int vf,
	struct enic_port_profile *prev_pp, int *restore_pp);
int enic_process_get_pp_request(struct enic *enic, int vf,
	int request, u16 *response);
int enic_is_valid_pp_vf(struct enic *enic, int vf, int *err);

#endif /* _ENIC_PP_H_ */
