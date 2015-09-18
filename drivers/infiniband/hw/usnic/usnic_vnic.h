/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
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

#ifndef USNIC_VNIC_H_
#define USNIC_VNIC_H_

#include <linux/pci.h>

#include "vnic_dev.h"

/*                      =USNIC_VNIC_RES_TYPE= =VNIC_RES=   =DESC= */
#define USNIC_VNIC_RES_TYPES \
	DEFINE_USNIC_VNIC_RES_AT(EOL, RES_TYPE_EOL, "EOL", 0) \
	DEFINE_USNIC_VNIC_RES(WQ, RES_TYPE_WQ, "WQ") \
	DEFINE_USNIC_VNIC_RES(RQ, RES_TYPE_RQ, "RQ") \
	DEFINE_USNIC_VNIC_RES(CQ, RES_TYPE_CQ, "CQ") \
	DEFINE_USNIC_VNIC_RES(INTR, RES_TYPE_INTR_CTRL, "INT") \
	DEFINE_USNIC_VNIC_RES(MAX, RES_TYPE_MAX, "MAX")\

#define DEFINE_USNIC_VNIC_RES_AT(usnic_vnic_res_t, vnic_res_type, desc, val) \
	USNIC_VNIC_RES_TYPE_##usnic_vnic_res_t = val,
#define DEFINE_USNIC_VNIC_RES(usnic_vnic_res_t, vnic_res_type, desc) \
	USNIC_VNIC_RES_TYPE_##usnic_vnic_res_t,
enum usnic_vnic_res_type {
	USNIC_VNIC_RES_TYPES
};
#undef DEFINE_USNIC_VNIC_RES
#undef DEFINE_USNIC_VNIC_RES_AT

struct usnic_vnic_res {
	enum usnic_vnic_res_type	type;
	unsigned int			vnic_idx;
	struct usnic_vnic		*vnic;
	void __iomem			*ctrl;
	void				*owner;
};

struct usnic_vnic_res_chunk {
	enum usnic_vnic_res_type	type;
	int				cnt;
	int				free_cnt;
	struct usnic_vnic_res		**res;
	struct usnic_vnic		*vnic;
};

struct usnic_vnic_res_desc {
	enum usnic_vnic_res_type	type;
	uint16_t			cnt;
};

struct usnic_vnic_res_spec {
	struct usnic_vnic_res_desc resources[USNIC_VNIC_RES_TYPE_MAX];
};

const char *usnic_vnic_res_type_to_str(enum usnic_vnic_res_type res_type);
const char *usnic_vnic_pci_name(struct usnic_vnic *vnic);
int usnic_vnic_dump(struct usnic_vnic *vnic, char *buf, int buf_sz,
			void *hdr_obj,
			int (*printtitle)(void *, char*, int),
			int (*printcols)(char *, int),
			int (*printrow)(void *, char *, int));
void usnic_vnic_res_spec_update(struct usnic_vnic_res_spec *spec,
				enum usnic_vnic_res_type trgt_type,
				u16 cnt);
int usnic_vnic_res_spec_satisfied(const struct usnic_vnic_res_spec *min_spec,
					struct usnic_vnic_res_spec *res_spec);
int usnic_vnic_spec_dump(char *buf, int buf_sz,
				struct usnic_vnic_res_spec *res_spec);
int usnic_vnic_check_room(struct usnic_vnic *vnic,
				struct usnic_vnic_res_spec *res_spec);
int usnic_vnic_res_cnt(struct usnic_vnic *vnic,
				enum usnic_vnic_res_type type);
int usnic_vnic_res_free_cnt(struct usnic_vnic *vnic,
				enum usnic_vnic_res_type type);
struct usnic_vnic_res_chunk *
usnic_vnic_get_resources(struct usnic_vnic *vnic,
				enum usnic_vnic_res_type type,
				int cnt,
				void *owner);
void usnic_vnic_put_resources(struct usnic_vnic_res_chunk *chunk);
struct pci_dev *usnic_vnic_get_pdev(struct usnic_vnic *vnic);
struct vnic_dev_bar *usnic_vnic_get_bar(struct usnic_vnic *vnic,
				int bar_num);
struct usnic_vnic *usnic_vnic_alloc(struct pci_dev *pdev);
void usnic_vnic_free(struct usnic_vnic *vnic);
u16 usnic_vnic_get_index(struct usnic_vnic *vnic);

#endif /*!USNIC_VNIC_H_*/
