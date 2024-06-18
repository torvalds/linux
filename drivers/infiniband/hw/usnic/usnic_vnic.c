/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
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
#include <linux/errno.h>
#include <linux/pci.h>

#include "usnic_ib.h"
#include "vnic_resource.h"
#include "usnic_log.h"
#include "usnic_vnic.h"

struct usnic_vnic {
	struct vnic_dev			*vdev;
	struct vnic_dev_bar		bar[PCI_NUM_RESOURCES];
	struct usnic_vnic_res_chunk	chunks[USNIC_VNIC_RES_TYPE_MAX];
	spinlock_t			res_lock;
};

static enum vnic_res_type _to_vnic_res_type(enum usnic_vnic_res_type res_type)
{
#define DEFINE_USNIC_VNIC_RES_AT(usnic_vnic_res_t, vnic_res_type, desc, val) \
		vnic_res_type,
#define DEFINE_USNIC_VNIC_RES(usnic_vnic_res_t, vnic_res_type, desc) \
		vnic_res_type,
	static enum vnic_res_type usnic_vnic_type_2_vnic_type[] = {
						USNIC_VNIC_RES_TYPES};
#undef DEFINE_USNIC_VNIC_RES
#undef DEFINE_USNIC_VNIC_RES_AT

	if (res_type >= USNIC_VNIC_RES_TYPE_MAX)
		return RES_TYPE_MAX;

	return usnic_vnic_type_2_vnic_type[res_type];
}

const char *usnic_vnic_res_type_to_str(enum usnic_vnic_res_type res_type)
{
#define DEFINE_USNIC_VNIC_RES_AT(usnic_vnic_res_t, vnic_res_type, desc, val) \
		desc,
#define DEFINE_USNIC_VNIC_RES(usnic_vnic_res_t, vnic_res_type, desc) \
		desc,
	static const char * const usnic_vnic_res_type_desc[] = {
						USNIC_VNIC_RES_TYPES};
#undef DEFINE_USNIC_VNIC_RES
#undef DEFINE_USNIC_VNIC_RES_AT

	if (res_type >= USNIC_VNIC_RES_TYPE_MAX)
		return "unknown";

	return usnic_vnic_res_type_desc[res_type];

}

const char *usnic_vnic_pci_name(struct usnic_vnic *vnic)
{
	return pci_name(usnic_vnic_get_pdev(vnic));
}

int usnic_vnic_dump(struct usnic_vnic *vnic, char *buf,
			int buf_sz,
			void *hdr_obj,
			int (*printtitle)(void *, char*, int),
			int (*printcols)(char *, int),
			int (*printrow)(void *, char *, int))
{
	struct usnic_vnic_res_chunk *chunk;
	struct usnic_vnic_res *res;
	struct vnic_dev_bar *bar0;
	int i, j, offset;

	offset = 0;
	bar0 = usnic_vnic_get_bar(vnic, 0);
	offset += scnprintf(buf + offset, buf_sz - offset,
			"VF:%hu BAR0 bus_addr=%pa vaddr=0x%p size=%ld ",
			usnic_vnic_get_index(vnic),
			&bar0->bus_addr,
			bar0->vaddr, bar0->len);
	if (printtitle)
		offset += printtitle(hdr_obj, buf + offset, buf_sz - offset);
	offset += scnprintf(buf + offset, buf_sz - offset, "\n");
	offset += scnprintf(buf + offset, buf_sz - offset,
			"|RES\t|CTRL_PIN\t\t|IN_USE\t");
	if (printcols)
		offset += printcols(buf + offset, buf_sz - offset);
	offset += scnprintf(buf + offset, buf_sz - offset, "\n");

	spin_lock(&vnic->res_lock);
	for (i = 0; i < ARRAY_SIZE(vnic->chunks); i++) {
		chunk = &vnic->chunks[i];
		for (j = 0; j < chunk->cnt; j++) {
			res = chunk->res[j];
			offset += scnprintf(buf + offset, buf_sz - offset,
					"|%s[%u]\t|0x%p\t|%u\t",
					usnic_vnic_res_type_to_str(res->type),
					res->vnic_idx, res->ctrl, !!res->owner);
			if (printrow) {
				offset += printrow(res->owner, buf + offset,
							buf_sz - offset);
			}
			offset += scnprintf(buf + offset, buf_sz - offset,
						"\n");
		}
	}
	spin_unlock(&vnic->res_lock);
	return offset;
}

void usnic_vnic_res_spec_update(struct usnic_vnic_res_spec *spec,
				enum usnic_vnic_res_type trgt_type,
				u16 cnt)
{
	int i;

	for (i = 0; i < USNIC_VNIC_RES_TYPE_MAX; i++) {
		if (spec->resources[i].type == trgt_type) {
			spec->resources[i].cnt = cnt;
			return;
		}
	}

	WARN_ON(1);
}

int usnic_vnic_res_spec_satisfied(const struct usnic_vnic_res_spec *min_spec,
					struct usnic_vnic_res_spec *res_spec)
{
	int found, i, j;

	for (i = 0; i < USNIC_VNIC_RES_TYPE_MAX; i++) {
		found = 0;

		for (j = 0; j < USNIC_VNIC_RES_TYPE_MAX; j++) {
			if (res_spec->resources[i].type !=
				min_spec->resources[i].type)
				continue;
			found = 1;
			if (min_spec->resources[i].cnt >
					res_spec->resources[i].cnt)
				return -EINVAL;
			break;
		}

		if (!found)
			return -EINVAL;
	}
	return 0;
}

int usnic_vnic_spec_dump(char *buf, int buf_sz,
				struct usnic_vnic_res_spec *res_spec)
{
	enum usnic_vnic_res_type res_type;
	int res_cnt;
	int i;
	int offset = 0;

	for (i = 0; i < USNIC_VNIC_RES_TYPE_MAX; i++) {
		res_type = res_spec->resources[i].type;
		res_cnt = res_spec->resources[i].cnt;
		offset += scnprintf(buf + offset, buf_sz - offset,
				"Res: %s Cnt: %d ",
				usnic_vnic_res_type_to_str(res_type),
				res_cnt);
	}

	return offset;
}

int usnic_vnic_check_room(struct usnic_vnic *vnic,
				struct usnic_vnic_res_spec *res_spec)
{
	int i;
	enum usnic_vnic_res_type res_type;
	int res_cnt;

	for (i = 0; i < USNIC_VNIC_RES_TYPE_MAX; i++) {
		res_type = res_spec->resources[i].type;
		res_cnt = res_spec->resources[i].cnt;

		if (res_type == USNIC_VNIC_RES_TYPE_EOL)
			break;

		if (res_cnt > usnic_vnic_res_free_cnt(vnic, res_type))
			return -EBUSY;
	}

	return 0;
}

int usnic_vnic_res_cnt(struct usnic_vnic *vnic,
				enum usnic_vnic_res_type type)
{
	return vnic->chunks[type].cnt;
}

int usnic_vnic_res_free_cnt(struct usnic_vnic *vnic,
				enum usnic_vnic_res_type type)
{
	return vnic->chunks[type].free_cnt;
}

struct usnic_vnic_res_chunk *
usnic_vnic_get_resources(struct usnic_vnic *vnic, enum usnic_vnic_res_type type,
				int cnt, void *owner)
{
	struct usnic_vnic_res_chunk *src, *ret;
	struct usnic_vnic_res *res;
	int i;

	if (usnic_vnic_res_free_cnt(vnic, type) < cnt || cnt < 0 || !owner)
		return ERR_PTR(-EINVAL);

	ret = kzalloc(sizeof(*ret), GFP_ATOMIC);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	if (cnt > 0) {
		ret->res = kcalloc(cnt, sizeof(*(ret->res)), GFP_ATOMIC);
		if (!ret->res) {
			kfree(ret);
			return ERR_PTR(-ENOMEM);
		}

		spin_lock(&vnic->res_lock);
		src = &vnic->chunks[type];
		for (i = 0; i < src->cnt && ret->cnt < cnt; i++) {
			res = src->res[i];
			if (!res->owner) {
				src->free_cnt--;
				res->owner = owner;
				ret->res[ret->cnt++] = res;
			}
		}

		spin_unlock(&vnic->res_lock);
	}
	ret->type = type;
	ret->vnic = vnic;
	WARN_ON(ret->cnt != cnt);

	return ret;
}

void usnic_vnic_put_resources(struct usnic_vnic_res_chunk *chunk)
{

	struct usnic_vnic_res *res;
	int i;
	struct usnic_vnic *vnic = chunk->vnic;

	if (chunk->cnt > 0) {
		spin_lock(&vnic->res_lock);
		while ((i = --chunk->cnt) >= 0) {
			res = chunk->res[i];
			chunk->res[i] = NULL;
			res->owner = NULL;
			vnic->chunks[res->type].free_cnt++;
		}
		spin_unlock(&vnic->res_lock);
	}

	kfree(chunk->res);
	kfree(chunk);
}

u16 usnic_vnic_get_index(struct usnic_vnic *vnic)
{
	return usnic_vnic_get_pdev(vnic)->devfn - 1;
}

static int usnic_vnic_alloc_res_chunk(struct usnic_vnic *vnic,
					enum usnic_vnic_res_type type,
					struct usnic_vnic_res_chunk *chunk)
{
	int cnt, err, i;
	struct usnic_vnic_res *res;

	cnt = vnic_dev_get_res_count(vnic->vdev, _to_vnic_res_type(type));
	if (cnt < 1) {
		usnic_err("Wrong res count with cnt %d\n", cnt);
		return -EINVAL;
	}

	chunk->cnt = chunk->free_cnt = cnt;
	chunk->res = kcalloc(cnt, sizeof(*(chunk->res)), GFP_KERNEL);
	if (!chunk->res)
		return -ENOMEM;

	for (i = 0; i < cnt; i++) {
		res = kzalloc(sizeof(*res), GFP_KERNEL);
		if (!res) {
			err = -ENOMEM;
			goto fail;
		}
		res->type = type;
		res->vnic_idx = i;
		res->vnic = vnic;
		res->ctrl = vnic_dev_get_res(vnic->vdev,
						_to_vnic_res_type(type), i);
		chunk->res[i] = res;
	}

	chunk->vnic = vnic;
	return 0;
fail:
	for (i--; i >= 0; i--)
		kfree(chunk->res[i]);
	kfree(chunk->res);
	return err;
}

static void usnic_vnic_free_res_chunk(struct usnic_vnic_res_chunk *chunk)
{
	int i;
	for (i = 0; i < chunk->cnt; i++)
		kfree(chunk->res[i]);
	kfree(chunk->res);
}

static int usnic_vnic_discover_resources(struct pci_dev *pdev,
						struct usnic_vnic *vnic)
{
	enum usnic_vnic_res_type res_type;
	int i;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(vnic->bar); i++) {
		if (!(pci_resource_flags(pdev, i) & IORESOURCE_MEM))
			continue;
		vnic->bar[i].len = pci_resource_len(pdev, i);
		vnic->bar[i].vaddr = pci_iomap(pdev, i, vnic->bar[i].len);
		if (!vnic->bar[i].vaddr) {
			usnic_err("Cannot memory-map BAR %d, aborting\n",
					i);
			err = -ENODEV;
			goto out_clean_bar;
		}
		vnic->bar[i].bus_addr = pci_resource_start(pdev, i);
	}

	vnic->vdev = vnic_dev_register(NULL, pdev, pdev, vnic->bar,
			ARRAY_SIZE(vnic->bar));
	if (!vnic->vdev) {
		usnic_err("Failed to register device %s\n",
				pci_name(pdev));
		err = -EINVAL;
		goto out_clean_bar;
	}

	for (res_type = USNIC_VNIC_RES_TYPE_EOL + 1;
			res_type < USNIC_VNIC_RES_TYPE_MAX; res_type++) {
		err = usnic_vnic_alloc_res_chunk(vnic, res_type,
						&vnic->chunks[res_type]);
		if (err)
			goto out_clean_chunks;
	}

	return 0;

out_clean_chunks:
	for (res_type--; res_type > USNIC_VNIC_RES_TYPE_EOL; res_type--)
		usnic_vnic_free_res_chunk(&vnic->chunks[res_type]);
	vnic_dev_unregister(vnic->vdev);
out_clean_bar:
	for (i = 0; i < ARRAY_SIZE(vnic->bar); i++) {
		if (!(pci_resource_flags(pdev, i) & IORESOURCE_MEM))
			continue;
		if (!vnic->bar[i].vaddr)
			break;

		iounmap(vnic->bar[i].vaddr);
	}

	return err;
}

struct pci_dev *usnic_vnic_get_pdev(struct usnic_vnic *vnic)
{
	return vnic_dev_get_pdev(vnic->vdev);
}

struct vnic_dev_bar *usnic_vnic_get_bar(struct usnic_vnic *vnic,
				int bar_num)
{
	return (bar_num < ARRAY_SIZE(vnic->bar)) ? &vnic->bar[bar_num] : NULL;
}

static void usnic_vnic_release_resources(struct usnic_vnic *vnic)
{
	int i;
	struct pci_dev *pdev;
	enum usnic_vnic_res_type res_type;

	pdev = usnic_vnic_get_pdev(vnic);

	for (res_type = USNIC_VNIC_RES_TYPE_EOL + 1;
			res_type < USNIC_VNIC_RES_TYPE_MAX; res_type++)
		usnic_vnic_free_res_chunk(&vnic->chunks[res_type]);

	vnic_dev_unregister(vnic->vdev);

	for (i = 0; i < ARRAY_SIZE(vnic->bar); i++) {
		if (!(pci_resource_flags(pdev, i) & IORESOURCE_MEM))
			continue;
		iounmap(vnic->bar[i].vaddr);
	}
}

struct usnic_vnic *usnic_vnic_alloc(struct pci_dev *pdev)
{
	struct usnic_vnic *vnic;
	int err = 0;

	if (!pci_is_enabled(pdev)) {
		usnic_err("PCI dev %s is disabled\n", pci_name(pdev));
		return ERR_PTR(-EINVAL);
	}

	vnic = kzalloc(sizeof(*vnic), GFP_KERNEL);
	if (!vnic)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&vnic->res_lock);

	err = usnic_vnic_discover_resources(pdev, vnic);
	if (err) {
		usnic_err("Failed to discover %s resources with err %d\n",
				pci_name(pdev), err);
		goto out_free_vnic;
	}

	usnic_dbg("Allocated vnic for %s\n", usnic_vnic_pci_name(vnic));

	return vnic;

out_free_vnic:
	kfree(vnic);

	return ERR_PTR(err);
}

void usnic_vnic_free(struct usnic_vnic *vnic)
{
	usnic_vnic_release_resources(vnic);
	kfree(vnic);
}
