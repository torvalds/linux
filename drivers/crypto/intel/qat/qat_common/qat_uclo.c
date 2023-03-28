// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci_ids.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "icp_qat_uclo.h"
#include "icp_qat_hal.h"
#include "icp_qat_fw_loader_handle.h"

#define UWORD_CPYBUF_SIZE 1024
#define INVLD_UWORD 0xffffffffffull
#define PID_MINOR_REV 0xf
#define PID_MAJOR_REV (0xf << 4)

static int qat_uclo_init_ae_data(struct icp_qat_uclo_objhandle *obj_handle,
				 unsigned int ae, unsigned int image_num)
{
	struct icp_qat_uclo_aedata *ae_data;
	struct icp_qat_uclo_encapme *encap_image;
	struct icp_qat_uclo_page *page = NULL;
	struct icp_qat_uclo_aeslice *ae_slice = NULL;

	ae_data = &obj_handle->ae_data[ae];
	encap_image = &obj_handle->ae_uimage[image_num];
	ae_slice = &ae_data->ae_slices[ae_data->slice_num];
	ae_slice->encap_image = encap_image;

	if (encap_image->img_ptr) {
		ae_slice->ctx_mask_assigned =
					encap_image->img_ptr->ctx_assigned;
		ae_data->eff_ustore_size = obj_handle->ustore_phy_size;
	} else {
		ae_slice->ctx_mask_assigned = 0;
	}
	ae_slice->region = kzalloc(sizeof(*ae_slice->region), GFP_KERNEL);
	if (!ae_slice->region)
		return -ENOMEM;
	ae_slice->page = kzalloc(sizeof(*ae_slice->page), GFP_KERNEL);
	if (!ae_slice->page)
		goto out_err;
	page = ae_slice->page;
	page->encap_page = encap_image->page;
	ae_slice->page->region = ae_slice->region;
	ae_data->slice_num++;
	return 0;
out_err:
	kfree(ae_slice->region);
	ae_slice->region = NULL;
	return -ENOMEM;
}

static int qat_uclo_free_ae_data(struct icp_qat_uclo_aedata *ae_data)
{
	unsigned int i;

	if (!ae_data) {
		pr_err("QAT: bad argument, ae_data is NULL\n ");
		return -EINVAL;
	}

	for (i = 0; i < ae_data->slice_num; i++) {
		kfree(ae_data->ae_slices[i].region);
		ae_data->ae_slices[i].region = NULL;
		kfree(ae_data->ae_slices[i].page);
		ae_data->ae_slices[i].page = NULL;
	}
	return 0;
}

static char *qat_uclo_get_string(struct icp_qat_uof_strtable *str_table,
				 unsigned int str_offset)
{
	if (!str_table->table_len || str_offset > str_table->table_len)
		return NULL;
	return (char *)(((uintptr_t)(str_table->strings)) + str_offset);
}

static int qat_uclo_check_uof_format(struct icp_qat_uof_filehdr *hdr)
{
	int maj = hdr->maj_ver & 0xff;
	int min = hdr->min_ver & 0xff;

	if (hdr->file_id != ICP_QAT_UOF_FID) {
		pr_err("QAT: Invalid header 0x%x\n", hdr->file_id);
		return -EINVAL;
	}
	if (min != ICP_QAT_UOF_MINVER || maj != ICP_QAT_UOF_MAJVER) {
		pr_err("QAT: bad UOF version, major 0x%x, minor 0x%x\n",
		       maj, min);
		return -EINVAL;
	}
	return 0;
}

static int qat_uclo_check_suof_format(struct icp_qat_suof_filehdr *suof_hdr)
{
	int maj = suof_hdr->maj_ver & 0xff;
	int min = suof_hdr->min_ver & 0xff;

	if (suof_hdr->file_id != ICP_QAT_SUOF_FID) {
		pr_err("QAT: invalid header 0x%x\n", suof_hdr->file_id);
		return -EINVAL;
	}
	if (suof_hdr->fw_type != 0) {
		pr_err("QAT: unsupported firmware type\n");
		return -EINVAL;
	}
	if (suof_hdr->num_chunks <= 0x1) {
		pr_err("QAT: SUOF chunk amount is incorrect\n");
		return -EINVAL;
	}
	if (maj != ICP_QAT_SUOF_MAJVER || min != ICP_QAT_SUOF_MINVER) {
		pr_err("QAT: bad SUOF version, major 0x%x, minor 0x%x\n",
		       maj, min);
		return -EINVAL;
	}
	return 0;
}

static void qat_uclo_wr_sram_by_words(struct icp_qat_fw_loader_handle *handle,
				      unsigned int addr, unsigned int *val,
				      unsigned int num_in_bytes)
{
	unsigned int outval;
	unsigned char *ptr = (unsigned char *)val;

	while (num_in_bytes) {
		memcpy(&outval, ptr, 4);
		SRAM_WRITE(handle, addr, outval);
		num_in_bytes -= 4;
		ptr += 4;
		addr += 4;
	}
}

static void qat_uclo_wr_umem_by_words(struct icp_qat_fw_loader_handle *handle,
				      unsigned char ae, unsigned int addr,
				      unsigned int *val,
				      unsigned int num_in_bytes)
{
	unsigned int outval;
	unsigned char *ptr = (unsigned char *)val;

	addr >>= 0x2; /* convert to uword address */

	while (num_in_bytes) {
		memcpy(&outval, ptr, 4);
		qat_hal_wr_umem(handle, ae, addr++, 1, &outval);
		num_in_bytes -= 4;
		ptr += 4;
	}
}

static void qat_uclo_batch_wr_umem(struct icp_qat_fw_loader_handle *handle,
				   unsigned char ae,
				   struct icp_qat_uof_batch_init
				   *umem_init_header)
{
	struct icp_qat_uof_batch_init *umem_init;

	if (!umem_init_header)
		return;
	umem_init = umem_init_header->next;
	while (umem_init) {
		unsigned int addr, *value, size;

		ae = umem_init->ae;
		addr = umem_init->addr;
		value = umem_init->value;
		size = umem_init->size;
		qat_uclo_wr_umem_by_words(handle, ae, addr, value, size);
		umem_init = umem_init->next;
	}
}

static void
qat_uclo_cleanup_batch_init_list(struct icp_qat_fw_loader_handle *handle,
				 struct icp_qat_uof_batch_init **base)
{
	struct icp_qat_uof_batch_init *umem_init;

	umem_init = *base;
	while (umem_init) {
		struct icp_qat_uof_batch_init *pre;

		pre = umem_init;
		umem_init = umem_init->next;
		kfree(pre);
	}
	*base = NULL;
}

static int qat_uclo_parse_num(char *str, unsigned int *num)
{
	char buf[16] = {0};
	unsigned long ae = 0;
	int i;

	strncpy(buf, str, 15);
	for (i = 0; i < 16; i++) {
		if (!isdigit(buf[i])) {
			buf[i] = '\0';
			break;
		}
	}
	if ((kstrtoul(buf, 10, &ae)))
		return -EFAULT;

	*num = (unsigned int)ae;
	return 0;
}

static int qat_uclo_fetch_initmem_ae(struct icp_qat_fw_loader_handle *handle,
				     struct icp_qat_uof_initmem *init_mem,
				     unsigned int size_range, unsigned int *ae)
{
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	char *str;

	if ((init_mem->addr + init_mem->num_in_bytes) > (size_range << 0x2)) {
		pr_err("QAT: initmem is out of range");
		return -EINVAL;
	}
	if (init_mem->scope != ICP_QAT_UOF_LOCAL_SCOPE) {
		pr_err("QAT: Memory scope for init_mem error\n");
		return -EINVAL;
	}
	str = qat_uclo_get_string(&obj_handle->str_table, init_mem->sym_name);
	if (!str) {
		pr_err("QAT: AE name assigned in UOF init table is NULL\n");
		return -EINVAL;
	}
	if (qat_uclo_parse_num(str, ae)) {
		pr_err("QAT: Parse num for AE number failed\n");
		return -EINVAL;
	}
	if (*ae >= ICP_QAT_UCLO_MAX_AE) {
		pr_err("QAT: ae %d out of range\n", *ae);
		return -EINVAL;
	}
	return 0;
}

static int qat_uclo_create_batch_init_list(struct icp_qat_fw_loader_handle
					   *handle, struct icp_qat_uof_initmem
					   *init_mem, unsigned int ae,
					   struct icp_qat_uof_batch_init
					   **init_tab_base)
{
	struct icp_qat_uof_batch_init *init_header, *tail;
	struct icp_qat_uof_batch_init *mem_init, *tail_old;
	struct icp_qat_uof_memvar_attr *mem_val_attr;
	unsigned int i, flag = 0;

	mem_val_attr =
		(struct icp_qat_uof_memvar_attr *)((uintptr_t)init_mem +
		sizeof(struct icp_qat_uof_initmem));

	init_header = *init_tab_base;
	if (!init_header) {
		init_header = kzalloc(sizeof(*init_header), GFP_KERNEL);
		if (!init_header)
			return -ENOMEM;
		init_header->size = 1;
		*init_tab_base = init_header;
		flag = 1;
	}
	tail_old = init_header;
	while (tail_old->next)
		tail_old = tail_old->next;
	tail = tail_old;
	for (i = 0; i < init_mem->val_attr_num; i++) {
		mem_init = kzalloc(sizeof(*mem_init), GFP_KERNEL);
		if (!mem_init)
			goto out_err;
		mem_init->ae = ae;
		mem_init->addr = init_mem->addr + mem_val_attr->offset_in_byte;
		mem_init->value = &mem_val_attr->value;
		mem_init->size = 4;
		mem_init->next = NULL;
		tail->next = mem_init;
		tail = mem_init;
		init_header->size += qat_hal_get_ins_num();
		mem_val_attr++;
	}
	return 0;
out_err:
	/* Do not free the list head unless we allocated it. */
	tail_old = tail_old->next;
	if (flag) {
		kfree(*init_tab_base);
		*init_tab_base = NULL;
	}

	while (tail_old) {
		mem_init = tail_old->next;
		kfree(tail_old);
		tail_old = mem_init;
	}
	return -ENOMEM;
}

static int qat_uclo_init_lmem_seg(struct icp_qat_fw_loader_handle *handle,
				  struct icp_qat_uof_initmem *init_mem)
{
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned int ae;

	if (qat_uclo_fetch_initmem_ae(handle, init_mem,
				      handle->chip_info->lm_size, &ae))
		return -EINVAL;
	if (qat_uclo_create_batch_init_list(handle, init_mem, ae,
					    &obj_handle->lm_init_tab[ae]))
		return -EINVAL;
	return 0;
}

static int qat_uclo_init_umem_seg(struct icp_qat_fw_loader_handle *handle,
				  struct icp_qat_uof_initmem *init_mem)
{
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned int ae, ustore_size, uaddr, i;
	struct icp_qat_uclo_aedata *aed;

	ustore_size = obj_handle->ustore_phy_size;
	if (qat_uclo_fetch_initmem_ae(handle, init_mem, ustore_size, &ae))
		return -EINVAL;
	if (qat_uclo_create_batch_init_list(handle, init_mem, ae,
					    &obj_handle->umem_init_tab[ae]))
		return -EINVAL;
	/* set the highest ustore address referenced */
	uaddr = (init_mem->addr + init_mem->num_in_bytes) >> 0x2;
	aed = &obj_handle->ae_data[ae];
	for (i = 0; i < aed->slice_num; i++) {
		if (aed->ae_slices[i].encap_image->uwords_num < uaddr)
			aed->ae_slices[i].encap_image->uwords_num = uaddr;
	}
	return 0;
}

static int qat_uclo_init_ae_memory(struct icp_qat_fw_loader_handle *handle,
				   struct icp_qat_uof_initmem *init_mem)
{
	switch (init_mem->region) {
	case ICP_QAT_UOF_LMEM_REGION:
		if (qat_uclo_init_lmem_seg(handle, init_mem))
			return -EINVAL;
		break;
	case ICP_QAT_UOF_UMEM_REGION:
		if (qat_uclo_init_umem_seg(handle, init_mem))
			return -EINVAL;
		break;
	default:
		pr_err("QAT: initmem region error. region type=0x%x\n",
		       init_mem->region);
		return -EINVAL;
	}
	return 0;
}

static int qat_uclo_init_ustore(struct icp_qat_fw_loader_handle *handle,
				struct icp_qat_uclo_encapme *image)
{
	unsigned int i;
	struct icp_qat_uclo_encap_page *page;
	struct icp_qat_uof_image *uof_image;
	unsigned char ae;
	unsigned int ustore_size;
	unsigned int patt_pos;
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned long cfg_ae_mask = handle->cfg_ae_mask;
	u64 *fill_data;

	uof_image = image->img_ptr;
	fill_data = kcalloc(ICP_QAT_UCLO_MAX_USTORE, sizeof(u64),
			    GFP_KERNEL);
	if (!fill_data)
		return -ENOMEM;
	for (i = 0; i < ICP_QAT_UCLO_MAX_USTORE; i++)
		memcpy(&fill_data[i], &uof_image->fill_pattern,
		       sizeof(u64));
	page = image->page;

	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		unsigned long ae_assigned = uof_image->ae_assigned;

		if (!test_bit(ae, &ae_assigned))
			continue;

		if (!test_bit(ae, &cfg_ae_mask))
			continue;

		ustore_size = obj_handle->ae_data[ae].eff_ustore_size;
		patt_pos = page->beg_addr_p + page->micro_words_num;

		qat_hal_wr_uwords(handle, (unsigned char)ae, 0,
				  page->beg_addr_p, &fill_data[0]);
		qat_hal_wr_uwords(handle, (unsigned char)ae, patt_pos,
				  ustore_size - patt_pos + 1,
				  &fill_data[page->beg_addr_p]);
	}
	kfree(fill_data);
	return 0;
}

static int qat_uclo_init_memory(struct icp_qat_fw_loader_handle *handle)
{
	int i, ae;
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	struct icp_qat_uof_initmem *initmem = obj_handle->init_mem_tab.init_mem;
	unsigned long ae_mask = handle->hal_handle->ae_mask;

	for (i = 0; i < obj_handle->init_mem_tab.entry_num; i++) {
		if (initmem->num_in_bytes) {
			if (qat_uclo_init_ae_memory(handle, initmem))
				return -EINVAL;
		}
		initmem = (struct icp_qat_uof_initmem *)((uintptr_t)(
			(uintptr_t)initmem +
			sizeof(struct icp_qat_uof_initmem)) +
			(sizeof(struct icp_qat_uof_memvar_attr) *
			initmem->val_attr_num));
	}

	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		if (qat_hal_batch_wr_lm(handle, ae,
					obj_handle->lm_init_tab[ae])) {
			pr_err("QAT: fail to batch init lmem for AE %d\n", ae);
			return -EINVAL;
		}
		qat_uclo_cleanup_batch_init_list(handle,
						 &obj_handle->lm_init_tab[ae]);
		qat_uclo_batch_wr_umem(handle, ae,
				       obj_handle->umem_init_tab[ae]);
		qat_uclo_cleanup_batch_init_list(handle,
						 &obj_handle->
						 umem_init_tab[ae]);
	}
	return 0;
}

static void *qat_uclo_find_chunk(struct icp_qat_uof_objhdr *obj_hdr,
				 char *chunk_id, void *cur)
{
	int i;
	struct icp_qat_uof_chunkhdr *chunk_hdr =
	    (struct icp_qat_uof_chunkhdr *)
	    ((uintptr_t)obj_hdr + sizeof(struct icp_qat_uof_objhdr));

	for (i = 0; i < obj_hdr->num_chunks; i++) {
		if ((cur < (void *)&chunk_hdr[i]) &&
		    !strncmp(chunk_hdr[i].chunk_id, chunk_id,
			     ICP_QAT_UOF_OBJID_LEN)) {
			return &chunk_hdr[i];
		}
	}
	return NULL;
}

static unsigned int qat_uclo_calc_checksum(unsigned int reg, int ch)
{
	int i;
	unsigned int topbit = 1 << 0xF;
	unsigned int inbyte = (unsigned int)((reg >> 0x18) ^ ch);

	reg ^= inbyte << 0x8;
	for (i = 0; i < 0x8; i++) {
		if (reg & topbit)
			reg = (reg << 1) ^ 0x1021;
		else
			reg <<= 1;
	}
	return reg & 0xFFFF;
}

static unsigned int qat_uclo_calc_str_checksum(char *ptr, int num)
{
	unsigned int chksum = 0;

	if (ptr)
		while (num--)
			chksum = qat_uclo_calc_checksum(chksum, *ptr++);
	return chksum;
}

static struct icp_qat_uclo_objhdr *
qat_uclo_map_chunk(char *buf, struct icp_qat_uof_filehdr *file_hdr,
		   char *chunk_id)
{
	struct icp_qat_uof_filechunkhdr *file_chunk;
	struct icp_qat_uclo_objhdr *obj_hdr;
	char *chunk;
	int i;

	file_chunk = (struct icp_qat_uof_filechunkhdr *)
		(buf + sizeof(struct icp_qat_uof_filehdr));
	for (i = 0; i < file_hdr->num_chunks; i++) {
		if (!strncmp(file_chunk->chunk_id, chunk_id,
			     ICP_QAT_UOF_OBJID_LEN)) {
			chunk = buf + file_chunk->offset;
			if (file_chunk->checksum != qat_uclo_calc_str_checksum(
				chunk, file_chunk->size))
				break;
			obj_hdr = kzalloc(sizeof(*obj_hdr), GFP_KERNEL);
			if (!obj_hdr)
				break;
			obj_hdr->file_buff = chunk;
			obj_hdr->checksum = file_chunk->checksum;
			obj_hdr->size = file_chunk->size;
			return obj_hdr;
		}
		file_chunk++;
	}
	return NULL;
}

static int
qat_uclo_check_image_compat(struct icp_qat_uof_encap_obj *encap_uof_obj,
			    struct icp_qat_uof_image *image)
{
	struct icp_qat_uof_objtable *uc_var_tab, *imp_var_tab, *imp_expr_tab;
	struct icp_qat_uof_objtable *neigh_reg_tab;
	struct icp_qat_uof_code_page *code_page;

	code_page = (struct icp_qat_uof_code_page *)
			((char *)image + sizeof(struct icp_qat_uof_image));
	uc_var_tab = (struct icp_qat_uof_objtable *)(encap_uof_obj->beg_uof +
		     code_page->uc_var_tab_offset);
	imp_var_tab = (struct icp_qat_uof_objtable *)(encap_uof_obj->beg_uof +
		      code_page->imp_var_tab_offset);
	imp_expr_tab = (struct icp_qat_uof_objtable *)
		       (encap_uof_obj->beg_uof +
		       code_page->imp_expr_tab_offset);
	if (uc_var_tab->entry_num || imp_var_tab->entry_num ||
	    imp_expr_tab->entry_num) {
		pr_err("QAT: UOF can't contain imported variable to be parsed\n");
		return -EINVAL;
	}
	neigh_reg_tab = (struct icp_qat_uof_objtable *)
			(encap_uof_obj->beg_uof +
			code_page->neigh_reg_tab_offset);
	if (neigh_reg_tab->entry_num) {
		pr_err("QAT: UOF can't contain neighbor register table\n");
		return -EINVAL;
	}
	if (image->numpages > 1) {
		pr_err("QAT: UOF can't contain multiple pages\n");
		return -EINVAL;
	}
	if (ICP_QAT_SHARED_USTORE_MODE(image->ae_mode)) {
		pr_err("QAT: UOF can't use shared control store feature\n");
		return -EFAULT;
	}
	if (RELOADABLE_CTX_SHARED_MODE(image->ae_mode)) {
		pr_err("QAT: UOF can't use reloadable feature\n");
		return -EFAULT;
	}
	return 0;
}

static void qat_uclo_map_image_page(struct icp_qat_uof_encap_obj
				     *encap_uof_obj,
				     struct icp_qat_uof_image *img,
				     struct icp_qat_uclo_encap_page *page)
{
	struct icp_qat_uof_code_page *code_page;
	struct icp_qat_uof_code_area *code_area;
	struct icp_qat_uof_objtable *uword_block_tab;
	struct icp_qat_uof_uword_block *uwblock;
	int i;

	code_page = (struct icp_qat_uof_code_page *)
			((char *)img + sizeof(struct icp_qat_uof_image));
	page->def_page = code_page->def_page;
	page->page_region = code_page->page_region;
	page->beg_addr_v = code_page->beg_addr_v;
	page->beg_addr_p = code_page->beg_addr_p;
	code_area = (struct icp_qat_uof_code_area *)(encap_uof_obj->beg_uof +
						code_page->code_area_offset);
	page->micro_words_num = code_area->micro_words_num;
	uword_block_tab = (struct icp_qat_uof_objtable *)
			  (encap_uof_obj->beg_uof +
			  code_area->uword_block_tab);
	page->uwblock_num = uword_block_tab->entry_num;
	uwblock = (struct icp_qat_uof_uword_block *)((char *)uword_block_tab +
			sizeof(struct icp_qat_uof_objtable));
	page->uwblock = (struct icp_qat_uclo_encap_uwblock *)uwblock;
	for (i = 0; i < uword_block_tab->entry_num; i++)
		page->uwblock[i].micro_words =
		(uintptr_t)encap_uof_obj->beg_uof + uwblock[i].uword_offset;
}

static int qat_uclo_map_uimage(struct icp_qat_uclo_objhandle *obj_handle,
			       struct icp_qat_uclo_encapme *ae_uimage,
			       int max_image)
{
	int i, j;
	struct icp_qat_uof_chunkhdr *chunk_hdr = NULL;
	struct icp_qat_uof_image *image;
	struct icp_qat_uof_objtable *ae_regtab;
	struct icp_qat_uof_objtable *init_reg_sym_tab;
	struct icp_qat_uof_objtable *sbreak_tab;
	struct icp_qat_uof_encap_obj *encap_uof_obj =
					&obj_handle->encap_uof_obj;

	for (j = 0; j < max_image; j++) {
		chunk_hdr = qat_uclo_find_chunk(encap_uof_obj->obj_hdr,
						ICP_QAT_UOF_IMAG, chunk_hdr);
		if (!chunk_hdr)
			break;
		image = (struct icp_qat_uof_image *)(encap_uof_obj->beg_uof +
						     chunk_hdr->offset);
		ae_regtab = (struct icp_qat_uof_objtable *)
			   (image->reg_tab_offset +
			   obj_handle->obj_hdr->file_buff);
		ae_uimage[j].ae_reg_num = ae_regtab->entry_num;
		ae_uimage[j].ae_reg = (struct icp_qat_uof_ae_reg *)
			(((char *)ae_regtab) +
			sizeof(struct icp_qat_uof_objtable));
		init_reg_sym_tab = (struct icp_qat_uof_objtable *)
				   (image->init_reg_sym_tab +
				   obj_handle->obj_hdr->file_buff);
		ae_uimage[j].init_regsym_num = init_reg_sym_tab->entry_num;
		ae_uimage[j].init_regsym = (struct icp_qat_uof_init_regsym *)
			(((char *)init_reg_sym_tab) +
			sizeof(struct icp_qat_uof_objtable));
		sbreak_tab = (struct icp_qat_uof_objtable *)
			(image->sbreak_tab + obj_handle->obj_hdr->file_buff);
		ae_uimage[j].sbreak_num = sbreak_tab->entry_num;
		ae_uimage[j].sbreak = (struct icp_qat_uof_sbreak *)
				      (((char *)sbreak_tab) +
				      sizeof(struct icp_qat_uof_objtable));
		ae_uimage[j].img_ptr = image;
		if (qat_uclo_check_image_compat(encap_uof_obj, image))
			goto out_err;
		ae_uimage[j].page =
			kzalloc(sizeof(struct icp_qat_uclo_encap_page),
				GFP_KERNEL);
		if (!ae_uimage[j].page)
			goto out_err;
		qat_uclo_map_image_page(encap_uof_obj, image,
					ae_uimage[j].page);
	}
	return j;
out_err:
	for (i = 0; i < j; i++)
		kfree(ae_uimage[i].page);
	return 0;
}

static int qat_uclo_map_ae(struct icp_qat_fw_loader_handle *handle, int max_ae)
{
	int i, ae;
	int mflag = 0;
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned long cfg_ae_mask = handle->cfg_ae_mask;

	for_each_set_bit(ae, &ae_mask, max_ae) {
		if (!test_bit(ae, &cfg_ae_mask))
			continue;

		for (i = 0; i < obj_handle->uimage_num; i++) {
			unsigned long ae_assigned = obj_handle->ae_uimage[i].img_ptr->ae_assigned;

			if (!test_bit(ae, &ae_assigned))
				continue;
			mflag = 1;
			if (qat_uclo_init_ae_data(obj_handle, ae, i))
				return -EINVAL;
		}
	}
	if (!mflag) {
		pr_err("QAT: uimage uses AE not set\n");
		return -EINVAL;
	}
	return 0;
}

static struct icp_qat_uof_strtable *
qat_uclo_map_str_table(struct icp_qat_uclo_objhdr *obj_hdr,
		       char *tab_name, struct icp_qat_uof_strtable *str_table)
{
	struct icp_qat_uof_chunkhdr *chunk_hdr;

	chunk_hdr = qat_uclo_find_chunk((struct icp_qat_uof_objhdr *)
					obj_hdr->file_buff, tab_name, NULL);
	if (chunk_hdr) {
		int hdr_size;

		memcpy(&str_table->table_len, obj_hdr->file_buff +
		       chunk_hdr->offset, sizeof(str_table->table_len));
		hdr_size = (char *)&str_table->strings - (char *)str_table;
		str_table->strings = (uintptr_t)obj_hdr->file_buff +
					chunk_hdr->offset + hdr_size;
		return str_table;
	}
	return NULL;
}

static void
qat_uclo_map_initmem_table(struct icp_qat_uof_encap_obj *encap_uof_obj,
			   struct icp_qat_uclo_init_mem_table *init_mem_tab)
{
	struct icp_qat_uof_chunkhdr *chunk_hdr;

	chunk_hdr = qat_uclo_find_chunk(encap_uof_obj->obj_hdr,
					ICP_QAT_UOF_IMEM, NULL);
	if (chunk_hdr) {
		memmove(&init_mem_tab->entry_num, encap_uof_obj->beg_uof +
			chunk_hdr->offset, sizeof(unsigned int));
		init_mem_tab->init_mem = (struct icp_qat_uof_initmem *)
		(encap_uof_obj->beg_uof + chunk_hdr->offset +
		sizeof(unsigned int));
	}
}

static unsigned int
qat_uclo_get_dev_type(struct icp_qat_fw_loader_handle *handle)
{
	switch (handle->pci_dev->device) {
	case PCI_DEVICE_ID_INTEL_QAT_DH895XCC:
		return ICP_QAT_AC_895XCC_DEV_TYPE;
	case PCI_DEVICE_ID_INTEL_QAT_C62X:
		return ICP_QAT_AC_C62X_DEV_TYPE;
	case PCI_DEVICE_ID_INTEL_QAT_C3XXX:
		return ICP_QAT_AC_C3XXX_DEV_TYPE;
	case ADF_4XXX_PCI_DEVICE_ID:
	case ADF_401XX_PCI_DEVICE_ID:
	case ADF_402XX_PCI_DEVICE_ID:
		return ICP_QAT_AC_4XXX_A_DEV_TYPE;
	default:
		pr_err("QAT: unsupported device 0x%x\n",
		       handle->pci_dev->device);
		return 0;
	}
}

static int qat_uclo_check_uof_compat(struct icp_qat_uclo_objhandle *obj_handle)
{
	unsigned int maj_ver, prod_type = obj_handle->prod_type;

	if (!(prod_type & obj_handle->encap_uof_obj.obj_hdr->ac_dev_type)) {
		pr_err("QAT: UOF type 0x%x doesn't match with platform 0x%x\n",
		       obj_handle->encap_uof_obj.obj_hdr->ac_dev_type,
		       prod_type);
		return -EINVAL;
	}
	maj_ver = obj_handle->prod_rev & 0xff;
	if (obj_handle->encap_uof_obj.obj_hdr->max_cpu_ver < maj_ver ||
	    obj_handle->encap_uof_obj.obj_hdr->min_cpu_ver > maj_ver) {
		pr_err("QAT: UOF majVer 0x%x out of range\n", maj_ver);
		return -EINVAL;
	}
	return 0;
}

static int qat_uclo_init_reg(struct icp_qat_fw_loader_handle *handle,
			     unsigned char ae, unsigned char ctx_mask,
			     enum icp_qat_uof_regtype reg_type,
			     unsigned short reg_addr, unsigned int value)
{
	switch (reg_type) {
	case ICP_GPA_ABS:
	case ICP_GPB_ABS:
		ctx_mask = 0;
		fallthrough;
	case ICP_GPA_REL:
	case ICP_GPB_REL:
		return qat_hal_init_gpr(handle, ae, ctx_mask, reg_type,
					reg_addr, value);
	case ICP_SR_ABS:
	case ICP_DR_ABS:
	case ICP_SR_RD_ABS:
	case ICP_DR_RD_ABS:
		ctx_mask = 0;
		fallthrough;
	case ICP_SR_REL:
	case ICP_DR_REL:
	case ICP_SR_RD_REL:
	case ICP_DR_RD_REL:
		return qat_hal_init_rd_xfer(handle, ae, ctx_mask, reg_type,
					    reg_addr, value);
	case ICP_SR_WR_ABS:
	case ICP_DR_WR_ABS:
		ctx_mask = 0;
		fallthrough;
	case ICP_SR_WR_REL:
	case ICP_DR_WR_REL:
		return qat_hal_init_wr_xfer(handle, ae, ctx_mask, reg_type,
					    reg_addr, value);
	case ICP_NEIGH_REL:
		return qat_hal_init_nn(handle, ae, ctx_mask, reg_addr, value);
	default:
		pr_err("QAT: UOF uses not supported reg type 0x%x\n", reg_type);
		return -EFAULT;
	}
	return 0;
}

static int qat_uclo_init_reg_sym(struct icp_qat_fw_loader_handle *handle,
				 unsigned int ae,
				 struct icp_qat_uclo_encapme *encap_ae)
{
	unsigned int i;
	unsigned char ctx_mask;
	struct icp_qat_uof_init_regsym *init_regsym;

	if (ICP_QAT_CTX_MODE(encap_ae->img_ptr->ae_mode) ==
	    ICP_QAT_UCLO_MAX_CTX)
		ctx_mask = 0xff;
	else
		ctx_mask = 0x55;

	for (i = 0; i < encap_ae->init_regsym_num; i++) {
		unsigned int exp_res;

		init_regsym = &encap_ae->init_regsym[i];
		exp_res = init_regsym->value;
		switch (init_regsym->init_type) {
		case ICP_QAT_UOF_INIT_REG:
			qat_uclo_init_reg(handle, ae, ctx_mask,
					  (enum icp_qat_uof_regtype)
					  init_regsym->reg_type,
					  (unsigned short)init_regsym->reg_addr,
					  exp_res);
			break;
		case ICP_QAT_UOF_INIT_REG_CTX:
			/* check if ctx is appropriate for the ctxMode */
			if (!((1 << init_regsym->ctx) & ctx_mask)) {
				pr_err("QAT: invalid ctx num = 0x%x\n",
				       init_regsym->ctx);
				return -EINVAL;
			}
			qat_uclo_init_reg(handle, ae,
					  (unsigned char)
					  (1 << init_regsym->ctx),
					  (enum icp_qat_uof_regtype)
					  init_regsym->reg_type,
					  (unsigned short)init_regsym->reg_addr,
					  exp_res);
			break;
		case ICP_QAT_UOF_INIT_EXPR:
			pr_err("QAT: INIT_EXPR feature not supported\n");
			return -EINVAL;
		case ICP_QAT_UOF_INIT_EXPR_ENDIAN_SWAP:
			pr_err("QAT: INIT_EXPR_ENDIAN_SWAP feature not supported\n");
			return -EINVAL;
		default:
			break;
		}
	}
	return 0;
}

static int qat_uclo_init_globals(struct icp_qat_fw_loader_handle *handle)
{
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	struct icp_qat_uclo_aedata *aed;
	unsigned int s, ae;

	if (obj_handle->global_inited)
		return 0;
	if (obj_handle->init_mem_tab.entry_num) {
		if (qat_uclo_init_memory(handle)) {
			pr_err("QAT: initialize memory failed\n");
			return -EINVAL;
		}
	}

	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		aed = &obj_handle->ae_data[ae];
		for (s = 0; s < aed->slice_num; s++) {
			if (!aed->ae_slices[s].encap_image)
				continue;
			if (qat_uclo_init_reg_sym(handle, ae, aed->ae_slices[s].encap_image))
				return -EINVAL;
		}
	}
	obj_handle->global_inited = 1;
	return 0;
}

static int qat_hal_set_modes(struct icp_qat_fw_loader_handle *handle,
			     struct icp_qat_uclo_objhandle *obj_handle,
			     unsigned char ae,
			     struct icp_qat_uof_image *uof_image)
{
	unsigned char mode;
	int ret;

	mode = ICP_QAT_CTX_MODE(uof_image->ae_mode);
	ret = qat_hal_set_ae_ctx_mode(handle, ae, mode);
	if (ret) {
		pr_err("QAT: qat_hal_set_ae_ctx_mode error\n");
		return ret;
	}
	if (handle->chip_info->nn) {
		mode = ICP_QAT_NN_MODE(uof_image->ae_mode);
		ret = qat_hal_set_ae_nn_mode(handle, ae, mode);
		if (ret) {
			pr_err("QAT: qat_hal_set_ae_nn_mode error\n");
			return ret;
		}
	}
	mode = ICP_QAT_LOC_MEM0_MODE(uof_image->ae_mode);
	ret = qat_hal_set_ae_lm_mode(handle, ae, ICP_LMEM0, mode);
	if (ret) {
		pr_err("QAT: qat_hal_set_ae_lm_mode LMEM0 error\n");
		return ret;
	}
	mode = ICP_QAT_LOC_MEM1_MODE(uof_image->ae_mode);
	ret = qat_hal_set_ae_lm_mode(handle, ae, ICP_LMEM1, mode);
	if (ret) {
		pr_err("QAT: qat_hal_set_ae_lm_mode LMEM1 error\n");
		return ret;
	}
	if (handle->chip_info->lm2lm3) {
		mode = ICP_QAT_LOC_MEM2_MODE(uof_image->ae_mode);
		ret = qat_hal_set_ae_lm_mode(handle, ae, ICP_LMEM2, mode);
		if (ret) {
			pr_err("QAT: qat_hal_set_ae_lm_mode LMEM2 error\n");
			return ret;
		}
		mode = ICP_QAT_LOC_MEM3_MODE(uof_image->ae_mode);
		ret = qat_hal_set_ae_lm_mode(handle, ae, ICP_LMEM3, mode);
		if (ret) {
			pr_err("QAT: qat_hal_set_ae_lm_mode LMEM3 error\n");
			return ret;
		}
		mode = ICP_QAT_LOC_TINDEX_MODE(uof_image->ae_mode);
		qat_hal_set_ae_tindex_mode(handle, ae, mode);
	}
	return 0;
}

static int qat_uclo_set_ae_mode(struct icp_qat_fw_loader_handle *handle)
{
	struct icp_qat_uof_image *uof_image;
	struct icp_qat_uclo_aedata *ae_data;
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned long cfg_ae_mask = handle->cfg_ae_mask;
	unsigned char ae, s;
	int error;

	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		if (!test_bit(ae, &cfg_ae_mask))
			continue;

		ae_data = &obj_handle->ae_data[ae];
		for (s = 0; s < min_t(unsigned int, ae_data->slice_num,
				      ICP_QAT_UCLO_MAX_CTX); s++) {
			if (!obj_handle->ae_data[ae].ae_slices[s].encap_image)
				continue;
			uof_image = ae_data->ae_slices[s].encap_image->img_ptr;
			error = qat_hal_set_modes(handle, obj_handle, ae,
						  uof_image);
			if (error)
				return error;
		}
	}
	return 0;
}

static void qat_uclo_init_uword_num(struct icp_qat_fw_loader_handle *handle)
{
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	struct icp_qat_uclo_encapme *image;
	int a;

	for (a = 0; a < obj_handle->uimage_num; a++) {
		image = &obj_handle->ae_uimage[a];
		image->uwords_num = image->page->beg_addr_p +
					image->page->micro_words_num;
	}
}

static int qat_uclo_parse_uof_obj(struct icp_qat_fw_loader_handle *handle)
{
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned int ae;

	obj_handle->encap_uof_obj.beg_uof = obj_handle->obj_hdr->file_buff;
	obj_handle->encap_uof_obj.obj_hdr = (struct icp_qat_uof_objhdr *)
					     obj_handle->obj_hdr->file_buff;
	obj_handle->uword_in_bytes = 6;
	obj_handle->prod_type = qat_uclo_get_dev_type(handle);
	obj_handle->prod_rev = PID_MAJOR_REV |
			(PID_MINOR_REV & handle->hal_handle->revision_id);
	if (qat_uclo_check_uof_compat(obj_handle)) {
		pr_err("QAT: UOF incompatible\n");
		return -EINVAL;
	}
	obj_handle->uword_buf = kcalloc(UWORD_CPYBUF_SIZE, sizeof(u64),
					GFP_KERNEL);
	if (!obj_handle->uword_buf)
		return -ENOMEM;
	obj_handle->ustore_phy_size = ICP_QAT_UCLO_MAX_USTORE;
	if (!obj_handle->obj_hdr->file_buff ||
	    !qat_uclo_map_str_table(obj_handle->obj_hdr, ICP_QAT_UOF_STRT,
				    &obj_handle->str_table)) {
		pr_err("QAT: UOF doesn't have effective images\n");
		goto out_err;
	}
	obj_handle->uimage_num =
		qat_uclo_map_uimage(obj_handle, obj_handle->ae_uimage,
				    ICP_QAT_UCLO_MAX_AE * ICP_QAT_UCLO_MAX_CTX);
	if (!obj_handle->uimage_num)
		goto out_err;
	if (qat_uclo_map_ae(handle, handle->hal_handle->ae_max_num)) {
		pr_err("QAT: Bad object\n");
		goto out_check_uof_aemask_err;
	}
	qat_uclo_init_uword_num(handle);
	qat_uclo_map_initmem_table(&obj_handle->encap_uof_obj,
				   &obj_handle->init_mem_tab);
	if (qat_uclo_set_ae_mode(handle))
		goto out_check_uof_aemask_err;
	return 0;
out_check_uof_aemask_err:
	for (ae = 0; ae < obj_handle->uimage_num; ae++)
		kfree(obj_handle->ae_uimage[ae].page);
out_err:
	kfree(obj_handle->uword_buf);
	return -EFAULT;
}

static int qat_uclo_map_suof_file_hdr(struct icp_qat_fw_loader_handle *handle,
				      struct icp_qat_suof_filehdr *suof_ptr,
				      int suof_size)
{
	unsigned int check_sum = 0;
	unsigned int min_ver_offset = 0;
	struct icp_qat_suof_handle *suof_handle = handle->sobj_handle;

	suof_handle->file_id = ICP_QAT_SUOF_FID;
	suof_handle->suof_buf = (char *)suof_ptr;
	suof_handle->suof_size = suof_size;
	min_ver_offset = suof_size - offsetof(struct icp_qat_suof_filehdr,
					      min_ver);
	check_sum = qat_uclo_calc_str_checksum((char *)&suof_ptr->min_ver,
					       min_ver_offset);
	if (check_sum != suof_ptr->check_sum) {
		pr_err("QAT: incorrect SUOF checksum\n");
		return -EINVAL;
	}
	suof_handle->check_sum = suof_ptr->check_sum;
	suof_handle->min_ver = suof_ptr->min_ver;
	suof_handle->maj_ver = suof_ptr->maj_ver;
	suof_handle->fw_type = suof_ptr->fw_type;
	return 0;
}

static void qat_uclo_map_simg(struct icp_qat_fw_loader_handle *handle,
			      struct icp_qat_suof_img_hdr *suof_img_hdr,
			      struct icp_qat_suof_chunk_hdr *suof_chunk_hdr)
{
	struct icp_qat_suof_handle *suof_handle = handle->sobj_handle;
	struct icp_qat_simg_ae_mode *ae_mode;
	struct icp_qat_suof_objhdr *suof_objhdr;

	suof_img_hdr->simg_buf  = (suof_handle->suof_buf +
				   suof_chunk_hdr->offset +
				   sizeof(*suof_objhdr));
	suof_img_hdr->simg_len = ((struct icp_qat_suof_objhdr *)(uintptr_t)
				  (suof_handle->suof_buf +
				   suof_chunk_hdr->offset))->img_length;

	suof_img_hdr->css_header = suof_img_hdr->simg_buf;
	suof_img_hdr->css_key = (suof_img_hdr->css_header +
				 sizeof(struct icp_qat_css_hdr));
	suof_img_hdr->css_signature = suof_img_hdr->css_key +
				      ICP_QAT_CSS_FWSK_MODULUS_LEN(handle) +
				      ICP_QAT_CSS_FWSK_EXPONENT_LEN(handle);
	suof_img_hdr->css_simg = suof_img_hdr->css_signature +
				 ICP_QAT_CSS_SIGNATURE_LEN(handle);

	ae_mode = (struct icp_qat_simg_ae_mode *)(suof_img_hdr->css_simg);
	suof_img_hdr->ae_mask = ae_mode->ae_mask;
	suof_img_hdr->simg_name = (unsigned long)&ae_mode->simg_name;
	suof_img_hdr->appmeta_data = (unsigned long)&ae_mode->appmeta_data;
	suof_img_hdr->fw_type = ae_mode->fw_type;
}

static void
qat_uclo_map_suof_symobjs(struct icp_qat_suof_handle *suof_handle,
			  struct icp_qat_suof_chunk_hdr *suof_chunk_hdr)
{
	char **sym_str = (char **)&suof_handle->sym_str;
	unsigned int *sym_size = &suof_handle->sym_size;
	struct icp_qat_suof_strtable *str_table_obj;

	*sym_size = *(unsigned int *)(uintptr_t)
		   (suof_chunk_hdr->offset + suof_handle->suof_buf);
	*sym_str = (char *)(uintptr_t)
		   (suof_handle->suof_buf + suof_chunk_hdr->offset +
		   sizeof(str_table_obj->tab_length));
}

static int qat_uclo_check_simg_compat(struct icp_qat_fw_loader_handle *handle,
				      struct icp_qat_suof_img_hdr *img_hdr)
{
	struct icp_qat_simg_ae_mode *img_ae_mode = NULL;
	unsigned int prod_rev, maj_ver, prod_type;

	prod_type = qat_uclo_get_dev_type(handle);
	img_ae_mode = (struct icp_qat_simg_ae_mode *)img_hdr->css_simg;
	prod_rev = PID_MAJOR_REV |
			 (PID_MINOR_REV & handle->hal_handle->revision_id);
	if (img_ae_mode->dev_type != prod_type) {
		pr_err("QAT: incompatible product type %x\n",
		       img_ae_mode->dev_type);
		return -EINVAL;
	}
	maj_ver = prod_rev & 0xff;
	if (maj_ver > img_ae_mode->devmax_ver ||
	    maj_ver < img_ae_mode->devmin_ver) {
		pr_err("QAT: incompatible device majver 0x%x\n", maj_ver);
		return -EINVAL;
	}
	return 0;
}

static void qat_uclo_del_suof(struct icp_qat_fw_loader_handle *handle)
{
	struct icp_qat_suof_handle *sobj_handle = handle->sobj_handle;

	kfree(sobj_handle->img_table.simg_hdr);
	sobj_handle->img_table.simg_hdr = NULL;
	kfree(handle->sobj_handle);
	handle->sobj_handle = NULL;
}

static void qat_uclo_tail_img(struct icp_qat_suof_img_hdr *suof_img_hdr,
			      unsigned int img_id, unsigned int num_simgs)
{
	struct icp_qat_suof_img_hdr img_header;

	if (img_id != num_simgs - 1) {
		memcpy(&img_header, &suof_img_hdr[num_simgs - 1],
		       sizeof(*suof_img_hdr));
		memcpy(&suof_img_hdr[num_simgs - 1], &suof_img_hdr[img_id],
		       sizeof(*suof_img_hdr));
		memcpy(&suof_img_hdr[img_id], &img_header,
		       sizeof(*suof_img_hdr));
	}
}

static int qat_uclo_map_suof(struct icp_qat_fw_loader_handle *handle,
			     struct icp_qat_suof_filehdr *suof_ptr,
			     int suof_size)
{
	struct icp_qat_suof_handle *suof_handle = handle->sobj_handle;
	struct icp_qat_suof_chunk_hdr *suof_chunk_hdr = NULL;
	struct icp_qat_suof_img_hdr *suof_img_hdr = NULL;
	int ret = 0, ae0_img = ICP_QAT_UCLO_MAX_AE;
	unsigned int i = 0;
	struct icp_qat_suof_img_hdr img_header;

	if (!suof_ptr || suof_size == 0) {
		pr_err("QAT: input parameter SUOF pointer/size is NULL\n");
		return -EINVAL;
	}
	if (qat_uclo_check_suof_format(suof_ptr))
		return -EINVAL;
	ret = qat_uclo_map_suof_file_hdr(handle, suof_ptr, suof_size);
	if (ret)
		return ret;
	suof_chunk_hdr = (struct icp_qat_suof_chunk_hdr *)
			 ((uintptr_t)suof_ptr + sizeof(*suof_ptr));

	qat_uclo_map_suof_symobjs(suof_handle, suof_chunk_hdr);
	suof_handle->img_table.num_simgs = suof_ptr->num_chunks - 1;

	if (suof_handle->img_table.num_simgs != 0) {
		suof_img_hdr = kcalloc(suof_handle->img_table.num_simgs,
				       sizeof(img_header),
				       GFP_KERNEL);
		if (!suof_img_hdr)
			return -ENOMEM;
		suof_handle->img_table.simg_hdr = suof_img_hdr;

		for (i = 0; i < suof_handle->img_table.num_simgs; i++) {
			qat_uclo_map_simg(handle, &suof_img_hdr[i],
					  &suof_chunk_hdr[1 + i]);
			ret = qat_uclo_check_simg_compat(handle,
							 &suof_img_hdr[i]);
			if (ret)
				return ret;
			suof_img_hdr[i].ae_mask &= handle->cfg_ae_mask;
			if ((suof_img_hdr[i].ae_mask & 0x1) != 0)
				ae0_img = i;
		}

		if (!handle->chip_info->tgroup_share_ustore) {
			qat_uclo_tail_img(suof_img_hdr, ae0_img,
					  suof_handle->img_table.num_simgs);
		}
	}
	return 0;
}

#define ADD_ADDR(high, low)  ((((u64)high) << 32) + low)
#define BITS_IN_DWORD 32

static int qat_uclo_auth_fw(struct icp_qat_fw_loader_handle *handle,
			    struct icp_qat_fw_auth_desc *desc)
{
	u32 fcu_sts, retry = 0;
	u32 fcu_ctl_csr, fcu_sts_csr;
	u32 fcu_dram_hi_csr, fcu_dram_lo_csr;
	u64 bus_addr;

	bus_addr = ADD_ADDR(desc->css_hdr_high, desc->css_hdr_low)
			   - sizeof(struct icp_qat_auth_chunk);

	fcu_ctl_csr = handle->chip_info->fcu_ctl_csr;
	fcu_sts_csr = handle->chip_info->fcu_sts_csr;
	fcu_dram_hi_csr = handle->chip_info->fcu_dram_addr_hi;
	fcu_dram_lo_csr = handle->chip_info->fcu_dram_addr_lo;

	SET_CAP_CSR(handle, fcu_dram_hi_csr, (bus_addr >> BITS_IN_DWORD));
	SET_CAP_CSR(handle, fcu_dram_lo_csr, bus_addr);
	SET_CAP_CSR(handle, fcu_ctl_csr, FCU_CTRL_CMD_AUTH);

	do {
		msleep(FW_AUTH_WAIT_PERIOD);
		fcu_sts = GET_CAP_CSR(handle, fcu_sts_csr);
		if ((fcu_sts & FCU_AUTH_STS_MASK) == FCU_STS_VERI_FAIL)
			goto auth_fail;
		if (((fcu_sts >> FCU_STS_AUTHFWLD_POS) & 0x1))
			if ((fcu_sts & FCU_AUTH_STS_MASK) == FCU_STS_VERI_DONE)
				return 0;
	} while (retry++ < FW_AUTH_MAX_RETRY);
auth_fail:
	pr_err("QAT: authentication error (FCU_STATUS = 0x%x),retry = %d\n",
	       fcu_sts & FCU_AUTH_STS_MASK, retry);
	return -EINVAL;
}

static bool qat_uclo_is_broadcast(struct icp_qat_fw_loader_handle *handle,
				  int imgid)
{
	struct icp_qat_suof_handle *sobj_handle;

	if (!handle->chip_info->tgroup_share_ustore)
		return false;

	sobj_handle = (struct icp_qat_suof_handle *)handle->sobj_handle;
	if (handle->hal_handle->admin_ae_mask &
	    sobj_handle->img_table.simg_hdr[imgid].ae_mask)
		return false;

	return true;
}

static int qat_uclo_broadcast_load_fw(struct icp_qat_fw_loader_handle *handle,
				      struct icp_qat_fw_auth_desc *desc)
{
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned long desc_ae_mask = desc->ae_mask;
	u32 fcu_sts, ae_broadcast_mask = 0;
	u32 fcu_loaded_csr, ae_loaded;
	u32 fcu_sts_csr, fcu_ctl_csr;
	unsigned int ae, retry = 0;

	if (handle->chip_info->tgroup_share_ustore) {
		fcu_ctl_csr = handle->chip_info->fcu_ctl_csr;
		fcu_sts_csr = handle->chip_info->fcu_sts_csr;
		fcu_loaded_csr = handle->chip_info->fcu_loaded_ae_csr;
	} else {
		pr_err("Chip 0x%x doesn't support broadcast load\n",
		       handle->pci_dev->device);
		return -EINVAL;
	}

	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		if (qat_hal_check_ae_active(handle, (unsigned char)ae)) {
			pr_err("QAT: Broadcast load failed. AE is not enabled or active.\n");
			return -EINVAL;
		}

		if (test_bit(ae, &desc_ae_mask))
			ae_broadcast_mask |= 1 << ae;
	}

	if (ae_broadcast_mask) {
		SET_CAP_CSR(handle, FCU_ME_BROADCAST_MASK_TYPE,
			    ae_broadcast_mask);

		SET_CAP_CSR(handle, fcu_ctl_csr, FCU_CTRL_CMD_LOAD);

		do {
			msleep(FW_AUTH_WAIT_PERIOD);
			fcu_sts = GET_CAP_CSR(handle, fcu_sts_csr);
			fcu_sts &= FCU_AUTH_STS_MASK;

			if (fcu_sts == FCU_STS_LOAD_FAIL) {
				pr_err("Broadcast load failed: 0x%x)\n", fcu_sts);
				return -EINVAL;
			} else if (fcu_sts == FCU_STS_LOAD_DONE) {
				ae_loaded = GET_CAP_CSR(handle, fcu_loaded_csr);
				ae_loaded >>= handle->chip_info->fcu_loaded_ae_pos;

				if ((ae_loaded & ae_broadcast_mask) == ae_broadcast_mask)
					break;
			}
		} while (retry++ < FW_AUTH_MAX_RETRY);

		if (retry > FW_AUTH_MAX_RETRY) {
			pr_err("QAT: broadcast load failed timeout %d\n", retry);
			return -EINVAL;
		}
	}
	return 0;
}

static int qat_uclo_simg_alloc(struct icp_qat_fw_loader_handle *handle,
			       struct icp_firml_dram_desc *dram_desc,
			       unsigned int size)
{
	void *vptr;
	dma_addr_t ptr;

	vptr = dma_alloc_coherent(&handle->pci_dev->dev,
				  size, &ptr, GFP_KERNEL);
	if (!vptr)
		return -ENOMEM;
	dram_desc->dram_base_addr_v = vptr;
	dram_desc->dram_bus_addr = ptr;
	dram_desc->dram_size = size;
	return 0;
}

static void qat_uclo_simg_free(struct icp_qat_fw_loader_handle *handle,
			       struct icp_firml_dram_desc *dram_desc)
{
	if (handle && dram_desc && dram_desc->dram_base_addr_v) {
		dma_free_coherent(&handle->pci_dev->dev,
				  (size_t)(dram_desc->dram_size),
				  dram_desc->dram_base_addr_v,
				  dram_desc->dram_bus_addr);
	}

	if (dram_desc)
		memset(dram_desc, 0, sizeof(*dram_desc));
}

static void qat_uclo_ummap_auth_fw(struct icp_qat_fw_loader_handle *handle,
				   struct icp_qat_fw_auth_desc **desc)
{
	struct icp_firml_dram_desc dram_desc;

	if (*desc) {
		dram_desc.dram_base_addr_v = *desc;
		dram_desc.dram_bus_addr = ((struct icp_qat_auth_chunk *)
					   (*desc))->chunk_bus_addr;
		dram_desc.dram_size = ((struct icp_qat_auth_chunk *)
				       (*desc))->chunk_size;
		qat_uclo_simg_free(handle, &dram_desc);
	}
}

static int qat_uclo_check_image(struct icp_qat_fw_loader_handle *handle,
				char *image, unsigned int size,
				unsigned int fw_type)
{
	char *fw_type_name = fw_type ? "MMP" : "AE";
	unsigned int css_dword_size = sizeof(u32);

	if (handle->chip_info->fw_auth) {
		struct icp_qat_css_hdr *css_hdr = (struct icp_qat_css_hdr *)image;
		unsigned int header_len = ICP_QAT_AE_IMG_OFFSET(handle);

		if ((css_hdr->header_len * css_dword_size) != header_len)
			goto err;
		if ((css_hdr->size * css_dword_size) != size)
			goto err;
		if (fw_type != css_hdr->fw_type)
			goto err;
		if (size <= header_len)
			goto err;
		size -= header_len;
	}

	if (fw_type == CSS_AE_FIRMWARE) {
		if (size < sizeof(struct icp_qat_simg_ae_mode *) +
		    ICP_QAT_SIMG_AE_INIT_SEQ_LEN)
			goto err;
		if (size > ICP_QAT_CSS_RSA4K_MAX_IMAGE_LEN)
			goto err;
	} else if (fw_type == CSS_MMP_FIRMWARE) {
		if (size > ICP_QAT_CSS_RSA3K_MAX_IMAGE_LEN)
			goto err;
	} else {
		pr_err("QAT: Unsupported firmware type\n");
		return -EINVAL;
	}
	return 0;

err:
	pr_err("QAT: Invalid %s firmware image\n", fw_type_name);
	return -EINVAL;
}

static int qat_uclo_map_auth_fw(struct icp_qat_fw_loader_handle *handle,
				char *image, unsigned int size,
				struct icp_qat_fw_auth_desc **desc)
{
	struct icp_qat_css_hdr *css_hdr = (struct icp_qat_css_hdr *)image;
	struct icp_qat_fw_auth_desc *auth_desc;
	struct icp_qat_auth_chunk *auth_chunk;
	u64 virt_addr,  bus_addr, virt_base;
	unsigned int length, simg_offset = sizeof(*auth_chunk);
	struct icp_qat_simg_ae_mode *simg_ae_mode;
	struct icp_firml_dram_desc img_desc;

	if (size > (ICP_QAT_AE_IMG_OFFSET(handle) + ICP_QAT_CSS_RSA4K_MAX_IMAGE_LEN)) {
		pr_err("QAT: error, input image size overflow %d\n", size);
		return -EINVAL;
	}
	length = (css_hdr->fw_type == CSS_AE_FIRMWARE) ?
		 ICP_QAT_CSS_AE_SIMG_LEN(handle) + simg_offset :
		 size + ICP_QAT_CSS_FWSK_PAD_LEN(handle) + simg_offset;
	if (qat_uclo_simg_alloc(handle, &img_desc, length)) {
		pr_err("QAT: error, allocate continuous dram fail\n");
		return -ENOMEM;
	}

	auth_chunk = img_desc.dram_base_addr_v;
	auth_chunk->chunk_size = img_desc.dram_size;
	auth_chunk->chunk_bus_addr = img_desc.dram_bus_addr;
	virt_base = (uintptr_t)img_desc.dram_base_addr_v + simg_offset;
	bus_addr  = img_desc.dram_bus_addr + simg_offset;
	auth_desc = img_desc.dram_base_addr_v;
	auth_desc->css_hdr_high = (unsigned int)(bus_addr >> BITS_IN_DWORD);
	auth_desc->css_hdr_low = (unsigned int)bus_addr;
	virt_addr = virt_base;

	memcpy((void *)(uintptr_t)virt_addr, image, sizeof(*css_hdr));
	/* pub key */
	bus_addr = ADD_ADDR(auth_desc->css_hdr_high, auth_desc->css_hdr_low) +
			   sizeof(*css_hdr);
	virt_addr = virt_addr + sizeof(*css_hdr);

	auth_desc->fwsk_pub_high = (unsigned int)(bus_addr >> BITS_IN_DWORD);
	auth_desc->fwsk_pub_low = (unsigned int)bus_addr;

	memcpy((void *)(uintptr_t)virt_addr,
	       (void *)(image + sizeof(*css_hdr)),
	       ICP_QAT_CSS_FWSK_MODULUS_LEN(handle));
	/* padding */
	memset((void *)(uintptr_t)(virt_addr + ICP_QAT_CSS_FWSK_MODULUS_LEN(handle)),
	       0, ICP_QAT_CSS_FWSK_PAD_LEN(handle));

	/* exponent */
	memcpy((void *)(uintptr_t)(virt_addr + ICP_QAT_CSS_FWSK_MODULUS_LEN(handle) +
	       ICP_QAT_CSS_FWSK_PAD_LEN(handle)),
	       (void *)(image + sizeof(*css_hdr) +
			ICP_QAT_CSS_FWSK_MODULUS_LEN(handle)),
	       sizeof(unsigned int));

	/* signature */
	bus_addr = ADD_ADDR(auth_desc->fwsk_pub_high,
			    auth_desc->fwsk_pub_low) +
		   ICP_QAT_CSS_FWSK_PUB_LEN(handle);
	virt_addr = virt_addr + ICP_QAT_CSS_FWSK_PUB_LEN(handle);
	auth_desc->signature_high = (unsigned int)(bus_addr >> BITS_IN_DWORD);
	auth_desc->signature_low = (unsigned int)bus_addr;

	memcpy((void *)(uintptr_t)virt_addr,
	       (void *)(image + sizeof(*css_hdr) +
	       ICP_QAT_CSS_FWSK_MODULUS_LEN(handle) +
	       ICP_QAT_CSS_FWSK_EXPONENT_LEN(handle)),
	       ICP_QAT_CSS_SIGNATURE_LEN(handle));

	bus_addr = ADD_ADDR(auth_desc->signature_high,
			    auth_desc->signature_low) +
		   ICP_QAT_CSS_SIGNATURE_LEN(handle);
	virt_addr += ICP_QAT_CSS_SIGNATURE_LEN(handle);

	auth_desc->img_high = (unsigned int)(bus_addr >> BITS_IN_DWORD);
	auth_desc->img_low = (unsigned int)bus_addr;
	auth_desc->img_len = size - ICP_QAT_AE_IMG_OFFSET(handle);
	memcpy((void *)(uintptr_t)virt_addr,
	       (void *)(image + ICP_QAT_AE_IMG_OFFSET(handle)),
	       auth_desc->img_len);
	virt_addr = virt_base;
	/* AE firmware */
	if (((struct icp_qat_css_hdr *)(uintptr_t)virt_addr)->fw_type ==
	    CSS_AE_FIRMWARE) {
		auth_desc->img_ae_mode_data_high = auth_desc->img_high;
		auth_desc->img_ae_mode_data_low = auth_desc->img_low;
		bus_addr = ADD_ADDR(auth_desc->img_ae_mode_data_high,
				    auth_desc->img_ae_mode_data_low) +
			   sizeof(struct icp_qat_simg_ae_mode);

		auth_desc->img_ae_init_data_high = (unsigned int)
						 (bus_addr >> BITS_IN_DWORD);
		auth_desc->img_ae_init_data_low = (unsigned int)bus_addr;
		bus_addr += ICP_QAT_SIMG_AE_INIT_SEQ_LEN;
		auth_desc->img_ae_insts_high = (unsigned int)
					     (bus_addr >> BITS_IN_DWORD);
		auth_desc->img_ae_insts_low = (unsigned int)bus_addr;
		virt_addr += sizeof(struct icp_qat_css_hdr);
		virt_addr += ICP_QAT_CSS_FWSK_PUB_LEN(handle);
		virt_addr += ICP_QAT_CSS_SIGNATURE_LEN(handle);
		simg_ae_mode = (struct icp_qat_simg_ae_mode *)(uintptr_t)virt_addr;
		auth_desc->ae_mask = simg_ae_mode->ae_mask & handle->cfg_ae_mask;
	} else {
		auth_desc->img_ae_insts_high = auth_desc->img_high;
		auth_desc->img_ae_insts_low = auth_desc->img_low;
	}
	*desc = auth_desc;
	return 0;
}

static int qat_uclo_load_fw(struct icp_qat_fw_loader_handle *handle,
			    struct icp_qat_fw_auth_desc *desc)
{
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	u32 fcu_sts_csr, fcu_ctl_csr;
	u32 loaded_aes, loaded_csr;
	unsigned int i;
	u32 fcu_sts;

	fcu_ctl_csr = handle->chip_info->fcu_ctl_csr;
	fcu_sts_csr = handle->chip_info->fcu_sts_csr;
	loaded_csr = handle->chip_info->fcu_loaded_ae_csr;

	for_each_set_bit(i, &ae_mask, handle->hal_handle->ae_max_num) {
		int retry = 0;

		if (!((desc->ae_mask >> i) & 0x1))
			continue;
		if (qat_hal_check_ae_active(handle, i)) {
			pr_err("QAT: AE %d is active\n", i);
			return -EINVAL;
		}
		SET_CAP_CSR(handle, fcu_ctl_csr,
			    (FCU_CTRL_CMD_LOAD |
			    (1 << FCU_CTRL_BROADCAST_POS) |
			    (i << FCU_CTRL_AE_POS)));

		do {
			msleep(FW_AUTH_WAIT_PERIOD);
			fcu_sts = GET_CAP_CSR(handle, fcu_sts_csr);
			if ((fcu_sts & FCU_AUTH_STS_MASK) ==
			    FCU_STS_LOAD_DONE) {
				loaded_aes = GET_CAP_CSR(handle, loaded_csr);
				loaded_aes >>= handle->chip_info->fcu_loaded_ae_pos;
				if (loaded_aes & (1 << i))
					break;
			}
		} while (retry++ < FW_AUTH_MAX_RETRY);
		if (retry > FW_AUTH_MAX_RETRY) {
			pr_err("QAT: firmware load failed timeout %x\n", retry);
			return -EINVAL;
		}
	}
	return 0;
}

static int qat_uclo_map_suof_obj(struct icp_qat_fw_loader_handle *handle,
				 void *addr_ptr, int mem_size)
{
	struct icp_qat_suof_handle *suof_handle;

	suof_handle = kzalloc(sizeof(*suof_handle), GFP_KERNEL);
	if (!suof_handle)
		return -ENOMEM;
	handle->sobj_handle = suof_handle;
	if (qat_uclo_map_suof(handle, addr_ptr, mem_size)) {
		qat_uclo_del_suof(handle);
		pr_err("QAT: map SUOF failed\n");
		return -EINVAL;
	}
	return 0;
}

int qat_uclo_wr_mimage(struct icp_qat_fw_loader_handle *handle,
		       void *addr_ptr, int mem_size)
{
	struct icp_qat_fw_auth_desc *desc = NULL;
	int status = 0;
	int ret;

	ret = qat_uclo_check_image(handle, addr_ptr, mem_size, CSS_MMP_FIRMWARE);
	if (ret)
		return ret;

	if (handle->chip_info->fw_auth) {
		status = qat_uclo_map_auth_fw(handle, addr_ptr, mem_size, &desc);
		if (!status)
			status = qat_uclo_auth_fw(handle, desc);
		qat_uclo_ummap_auth_fw(handle, &desc);
	} else {
		if (handle->chip_info->mmp_sram_size < mem_size) {
			pr_err("QAT: MMP size is too large: 0x%x\n", mem_size);
			return -EFBIG;
		}
		qat_uclo_wr_sram_by_words(handle, 0, addr_ptr, mem_size);
	}
	return status;
}

static int qat_uclo_map_uof_obj(struct icp_qat_fw_loader_handle *handle,
				void *addr_ptr, int mem_size)
{
	struct icp_qat_uof_filehdr *filehdr;
	struct icp_qat_uclo_objhandle *objhdl;

	objhdl = kzalloc(sizeof(*objhdl), GFP_KERNEL);
	if (!objhdl)
		return -ENOMEM;
	objhdl->obj_buf = kmemdup(addr_ptr, mem_size, GFP_KERNEL);
	if (!objhdl->obj_buf)
		goto out_objbuf_err;
	filehdr = (struct icp_qat_uof_filehdr *)objhdl->obj_buf;
	if (qat_uclo_check_uof_format(filehdr))
		goto out_objhdr_err;
	objhdl->obj_hdr = qat_uclo_map_chunk((char *)objhdl->obj_buf, filehdr,
					     ICP_QAT_UOF_OBJS);
	if (!objhdl->obj_hdr) {
		pr_err("QAT: object file chunk is null\n");
		goto out_objhdr_err;
	}
	handle->obj_handle = objhdl;
	if (qat_uclo_parse_uof_obj(handle))
		goto out_overlay_obj_err;
	return 0;

out_overlay_obj_err:
	handle->obj_handle = NULL;
	kfree(objhdl->obj_hdr);
out_objhdr_err:
	kfree(objhdl->obj_buf);
out_objbuf_err:
	kfree(objhdl);
	return -ENOMEM;
}

static int qat_uclo_map_mof_file_hdr(struct icp_qat_fw_loader_handle *handle,
				     struct icp_qat_mof_file_hdr *mof_ptr,
				     u32 mof_size)
{
	struct icp_qat_mof_handle *mobj_handle = handle->mobj_handle;
	unsigned int min_ver_offset;
	unsigned int checksum;

	mobj_handle->file_id = ICP_QAT_MOF_FID;
	mobj_handle->mof_buf = (char *)mof_ptr;
	mobj_handle->mof_size = mof_size;

	min_ver_offset = mof_size - offsetof(struct icp_qat_mof_file_hdr,
					     min_ver);
	checksum = qat_uclo_calc_str_checksum(&mof_ptr->min_ver,
					      min_ver_offset);
	if (checksum != mof_ptr->checksum) {
		pr_err("QAT: incorrect MOF checksum\n");
		return -EINVAL;
	}

	mobj_handle->checksum = mof_ptr->checksum;
	mobj_handle->min_ver = mof_ptr->min_ver;
	mobj_handle->maj_ver = mof_ptr->maj_ver;
	return 0;
}

static void qat_uclo_del_mof(struct icp_qat_fw_loader_handle *handle)
{
	struct icp_qat_mof_handle *mobj_handle = handle->mobj_handle;

	kfree(mobj_handle->obj_table.obj_hdr);
	mobj_handle->obj_table.obj_hdr = NULL;
	kfree(handle->mobj_handle);
	handle->mobj_handle = NULL;
}

static int qat_uclo_seek_obj_inside_mof(struct icp_qat_mof_handle *mobj_handle,
					char *obj_name, char **obj_ptr,
					unsigned int *obj_size)
{
	struct icp_qat_mof_objhdr *obj_hdr = mobj_handle->obj_table.obj_hdr;
	unsigned int i;

	for (i = 0; i < mobj_handle->obj_table.num_objs; i++) {
		if (!strncmp(obj_hdr[i].obj_name, obj_name,
			     ICP_QAT_SUOF_OBJ_NAME_LEN)) {
			*obj_ptr  = obj_hdr[i].obj_buf;
			*obj_size = obj_hdr[i].obj_size;
			return 0;
		}
	}

	pr_err("QAT: object %s is not found inside MOF\n", obj_name);
	return -EINVAL;
}

static int qat_uclo_map_obj_from_mof(struct icp_qat_mof_handle *mobj_handle,
				     struct icp_qat_mof_objhdr *mobj_hdr,
				     struct icp_qat_mof_obj_chunkhdr *obj_chunkhdr)
{
	u8 *obj;

	if (!strncmp(obj_chunkhdr->chunk_id, ICP_QAT_UOF_IMAG,
		     ICP_QAT_MOF_OBJ_CHUNKID_LEN)) {
		obj = mobj_handle->uobjs_hdr + obj_chunkhdr->offset;
	} else if (!strncmp(obj_chunkhdr->chunk_id, ICP_QAT_SUOF_IMAG,
			    ICP_QAT_MOF_OBJ_CHUNKID_LEN)) {
		obj = mobj_handle->sobjs_hdr + obj_chunkhdr->offset;
	} else {
		pr_err("QAT: unsupported chunk id\n");
		return -EINVAL;
	}
	mobj_hdr->obj_buf = obj;
	mobj_hdr->obj_size = (unsigned int)obj_chunkhdr->size;
	mobj_hdr->obj_name = obj_chunkhdr->name + mobj_handle->sym_str;
	return 0;
}

static int qat_uclo_map_objs_from_mof(struct icp_qat_mof_handle *mobj_handle)
{
	struct icp_qat_mof_obj_chunkhdr *uobj_chunkhdr;
	struct icp_qat_mof_obj_chunkhdr *sobj_chunkhdr;
	struct icp_qat_mof_obj_hdr *uobj_hdr;
	struct icp_qat_mof_obj_hdr *sobj_hdr;
	struct icp_qat_mof_objhdr *mobj_hdr;
	unsigned int uobj_chunk_num = 0;
	unsigned int sobj_chunk_num = 0;
	unsigned int *valid_chunk;
	int ret, i;

	uobj_hdr = (struct icp_qat_mof_obj_hdr *)mobj_handle->uobjs_hdr;
	sobj_hdr = (struct icp_qat_mof_obj_hdr *)mobj_handle->sobjs_hdr;
	if (uobj_hdr)
		uobj_chunk_num = uobj_hdr->num_chunks;
	if (sobj_hdr)
		sobj_chunk_num = sobj_hdr->num_chunks;

	mobj_hdr = kzalloc((uobj_chunk_num + sobj_chunk_num) *
			   sizeof(*mobj_hdr), GFP_KERNEL);
	if (!mobj_hdr)
		return -ENOMEM;

	mobj_handle->obj_table.obj_hdr = mobj_hdr;
	valid_chunk = &mobj_handle->obj_table.num_objs;
	uobj_chunkhdr = (struct icp_qat_mof_obj_chunkhdr *)
			 ((uintptr_t)uobj_hdr + sizeof(*uobj_hdr));
	sobj_chunkhdr = (struct icp_qat_mof_obj_chunkhdr *)
			((uintptr_t)sobj_hdr + sizeof(*sobj_hdr));

	/* map uof objects */
	for (i = 0; i < uobj_chunk_num; i++) {
		ret = qat_uclo_map_obj_from_mof(mobj_handle,
						&mobj_hdr[*valid_chunk],
						&uobj_chunkhdr[i]);
		if (ret)
			return ret;
		(*valid_chunk)++;
	}

	/* map suof objects */
	for (i = 0; i < sobj_chunk_num; i++) {
		ret = qat_uclo_map_obj_from_mof(mobj_handle,
						&mobj_hdr[*valid_chunk],
						&sobj_chunkhdr[i]);
		if (ret)
			return ret;
		(*valid_chunk)++;
	}

	if ((uobj_chunk_num + sobj_chunk_num) != *valid_chunk) {
		pr_err("QAT: inconsistent UOF/SUOF chunk amount\n");
		return -EINVAL;
	}
	return 0;
}

static void qat_uclo_map_mof_symobjs(struct icp_qat_mof_handle *mobj_handle,
				     struct icp_qat_mof_chunkhdr *mof_chunkhdr)
{
	char **sym_str = (char **)&mobj_handle->sym_str;
	unsigned int *sym_size = &mobj_handle->sym_size;
	struct icp_qat_mof_str_table *str_table_obj;

	*sym_size = *(unsigned int *)(uintptr_t)
		    (mof_chunkhdr->offset + mobj_handle->mof_buf);
	*sym_str = (char *)(uintptr_t)
		   (mobj_handle->mof_buf + mof_chunkhdr->offset +
		    sizeof(str_table_obj->tab_len));
}

static void qat_uclo_map_mof_chunk(struct icp_qat_mof_handle *mobj_handle,
				   struct icp_qat_mof_chunkhdr *mof_chunkhdr)
{
	char *chunk_id = mof_chunkhdr->chunk_id;

	if (!strncmp(chunk_id, ICP_QAT_MOF_SYM_OBJS, ICP_QAT_MOF_OBJ_ID_LEN))
		qat_uclo_map_mof_symobjs(mobj_handle, mof_chunkhdr);
	else if (!strncmp(chunk_id, ICP_QAT_UOF_OBJS, ICP_QAT_MOF_OBJ_ID_LEN))
		mobj_handle->uobjs_hdr = mobj_handle->mof_buf +
					 mof_chunkhdr->offset;
	else if (!strncmp(chunk_id, ICP_QAT_SUOF_OBJS, ICP_QAT_MOF_OBJ_ID_LEN))
		mobj_handle->sobjs_hdr = mobj_handle->mof_buf +
					 mof_chunkhdr->offset;
}

static int qat_uclo_check_mof_format(struct icp_qat_mof_file_hdr *mof_hdr)
{
	int maj = mof_hdr->maj_ver & 0xff;
	int min = mof_hdr->min_ver & 0xff;

	if (mof_hdr->file_id != ICP_QAT_MOF_FID) {
		pr_err("QAT: invalid header 0x%x\n", mof_hdr->file_id);
		return -EINVAL;
	}

	if (mof_hdr->num_chunks <= 0x1) {
		pr_err("QAT: MOF chunk amount is incorrect\n");
		return -EINVAL;
	}
	if (maj != ICP_QAT_MOF_MAJVER || min != ICP_QAT_MOF_MINVER) {
		pr_err("QAT: bad MOF version, major 0x%x, minor 0x%x\n",
		       maj, min);
		return -EINVAL;
	}
	return 0;
}

static int qat_uclo_map_mof_obj(struct icp_qat_fw_loader_handle *handle,
				struct icp_qat_mof_file_hdr *mof_ptr,
				u32 mof_size, char *obj_name, char **obj_ptr,
				unsigned int *obj_size)
{
	struct icp_qat_mof_chunkhdr *mof_chunkhdr;
	unsigned int file_id = mof_ptr->file_id;
	struct icp_qat_mof_handle *mobj_handle;
	unsigned short chunks_num;
	unsigned int i;
	int ret;

	if (file_id == ICP_QAT_UOF_FID || file_id == ICP_QAT_SUOF_FID) {
		if (obj_ptr)
			*obj_ptr = (char *)mof_ptr;
		if (obj_size)
			*obj_size = mof_size;
		return 0;
	}
	if (qat_uclo_check_mof_format(mof_ptr))
		return -EINVAL;

	mobj_handle = kzalloc(sizeof(*mobj_handle), GFP_KERNEL);
	if (!mobj_handle)
		return -ENOMEM;

	handle->mobj_handle = mobj_handle;
	ret = qat_uclo_map_mof_file_hdr(handle, mof_ptr, mof_size);
	if (ret)
		return ret;

	mof_chunkhdr = (void *)mof_ptr + sizeof(*mof_ptr);
	chunks_num = mof_ptr->num_chunks;

	/* Parse MOF file chunks */
	for (i = 0; i < chunks_num; i++)
		qat_uclo_map_mof_chunk(mobj_handle, &mof_chunkhdr[i]);

	/* All sym_objs uobjs and sobjs should be available */
	if (!mobj_handle->sym_str ||
	    (!mobj_handle->uobjs_hdr && !mobj_handle->sobjs_hdr))
		return -EINVAL;

	ret = qat_uclo_map_objs_from_mof(mobj_handle);
	if (ret)
		return ret;

	/* Seek specified uof object in MOF */
	return qat_uclo_seek_obj_inside_mof(mobj_handle, obj_name,
					    obj_ptr, obj_size);
}

int qat_uclo_map_obj(struct icp_qat_fw_loader_handle *handle,
		     void *addr_ptr, u32 mem_size, char *obj_name)
{
	char *obj_addr;
	u32 obj_size;
	int ret;

	BUILD_BUG_ON(ICP_QAT_UCLO_MAX_AE >=
		     (sizeof(handle->hal_handle->ae_mask) * 8));

	if (!handle || !addr_ptr || mem_size < 24)
		return -EINVAL;

	if (obj_name) {
		ret = qat_uclo_map_mof_obj(handle, addr_ptr, mem_size, obj_name,
					   &obj_addr, &obj_size);
		if (ret)
			return ret;
	} else {
		obj_addr = addr_ptr;
		obj_size = mem_size;
	}

	return (handle->chip_info->fw_auth) ?
			qat_uclo_map_suof_obj(handle, obj_addr, obj_size) :
			qat_uclo_map_uof_obj(handle, obj_addr, obj_size);
}

void qat_uclo_del_obj(struct icp_qat_fw_loader_handle *handle)
{
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned int a;

	if (handle->mobj_handle)
		qat_uclo_del_mof(handle);
	if (handle->sobj_handle)
		qat_uclo_del_suof(handle);
	if (!obj_handle)
		return;

	kfree(obj_handle->uword_buf);
	for (a = 0; a < obj_handle->uimage_num; a++)
		kfree(obj_handle->ae_uimage[a].page);

	for (a = 0; a < handle->hal_handle->ae_max_num; a++)
		qat_uclo_free_ae_data(&obj_handle->ae_data[a]);

	kfree(obj_handle->obj_hdr);
	kfree(obj_handle->obj_buf);
	kfree(obj_handle);
	handle->obj_handle = NULL;
}

static void qat_uclo_fill_uwords(struct icp_qat_uclo_objhandle *obj_handle,
				 struct icp_qat_uclo_encap_page *encap_page,
				 u64 *uword, unsigned int addr_p,
				 unsigned int raddr, u64 fill)
{
	unsigned int i, addr;
	u64 uwrd = 0;

	if (!encap_page) {
		*uword = fill;
		return;
	}
	addr = (encap_page->page_region) ? raddr : addr_p;
	for (i = 0; i < encap_page->uwblock_num; i++) {
		if (addr >= encap_page->uwblock[i].start_addr &&
		    addr <= encap_page->uwblock[i].start_addr +
		    encap_page->uwblock[i].words_num - 1) {
			addr -= encap_page->uwblock[i].start_addr;
			addr *= obj_handle->uword_in_bytes;
			memcpy(&uwrd, (void *)(((uintptr_t)
			       encap_page->uwblock[i].micro_words) + addr),
			       obj_handle->uword_in_bytes);
			uwrd = uwrd & GENMASK_ULL(43, 0);
		}
	}
	*uword = uwrd;
	if (*uword == INVLD_UWORD)
		*uword = fill;
}

static void qat_uclo_wr_uimage_raw_page(struct icp_qat_fw_loader_handle *handle,
					struct icp_qat_uclo_encap_page
					*encap_page, unsigned int ae)
{
	unsigned int uw_physical_addr, uw_relative_addr, i, words_num, cpylen;
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	u64 fill_pat;

	/* load the page starting at appropriate ustore address */
	/* get fill-pattern from an image -- they are all the same */
	memcpy(&fill_pat, obj_handle->ae_uimage[0].img_ptr->fill_pattern,
	       sizeof(u64));
	uw_physical_addr = encap_page->beg_addr_p;
	uw_relative_addr = 0;
	words_num = encap_page->micro_words_num;
	while (words_num) {
		if (words_num < UWORD_CPYBUF_SIZE)
			cpylen = words_num;
		else
			cpylen = UWORD_CPYBUF_SIZE;

		/* load the buffer */
		for (i = 0; i < cpylen; i++)
			qat_uclo_fill_uwords(obj_handle, encap_page,
					     &obj_handle->uword_buf[i],
					     uw_physical_addr + i,
					     uw_relative_addr + i, fill_pat);

		/* copy the buffer to ustore */
		qat_hal_wr_uwords(handle, (unsigned char)ae,
				  uw_physical_addr, cpylen,
				  obj_handle->uword_buf);

		uw_physical_addr += cpylen;
		uw_relative_addr += cpylen;
		words_num -= cpylen;
	}
}

static void qat_uclo_wr_uimage_page(struct icp_qat_fw_loader_handle *handle,
				    struct icp_qat_uof_image *image)
{
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned long cfg_ae_mask = handle->cfg_ae_mask;
	unsigned long ae_assigned = image->ae_assigned;
	struct icp_qat_uclo_aedata *aed;
	unsigned int ctx_mask, s;
	struct icp_qat_uclo_page *page;
	unsigned char ae;
	int ctx;

	if (ICP_QAT_CTX_MODE(image->ae_mode) == ICP_QAT_UCLO_MAX_CTX)
		ctx_mask = 0xff;
	else
		ctx_mask = 0x55;
	/* load the default page and set assigned CTX PC
	 * to the entrypoint address */
	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		if (!test_bit(ae, &cfg_ae_mask))
			continue;

		if (!test_bit(ae, &ae_assigned))
			continue;

		aed = &obj_handle->ae_data[ae];
		/* find the slice to which this image is assigned */
		for (s = 0; s < aed->slice_num; s++) {
			if (image->ctx_assigned &
			    aed->ae_slices[s].ctx_mask_assigned)
				break;
		}
		if (s >= aed->slice_num)
			continue;
		page = aed->ae_slices[s].page;
		if (!page->encap_page->def_page)
			continue;
		qat_uclo_wr_uimage_raw_page(handle, page->encap_page, ae);

		page = aed->ae_slices[s].page;
		for (ctx = 0; ctx < ICP_QAT_UCLO_MAX_CTX; ctx++)
			aed->ae_slices[s].cur_page[ctx] =
					(ctx_mask & (1 << ctx)) ? page : NULL;
		qat_hal_set_live_ctx(handle, (unsigned char)ae,
				     image->ctx_assigned);
		qat_hal_set_pc(handle, (unsigned char)ae, image->ctx_assigned,
			       image->entry_address);
	}
}

static int qat_uclo_wr_suof_img(struct icp_qat_fw_loader_handle *handle)
{
	unsigned int i;
	struct icp_qat_fw_auth_desc *desc = NULL;
	struct icp_qat_suof_handle *sobj_handle = handle->sobj_handle;
	struct icp_qat_suof_img_hdr *simg_hdr = sobj_handle->img_table.simg_hdr;
	int ret;

	for (i = 0; i < sobj_handle->img_table.num_simgs; i++) {
		ret = qat_uclo_check_image(handle, simg_hdr[i].simg_buf,
					   simg_hdr[i].simg_len,
					   CSS_AE_FIRMWARE);
		if (ret)
			return ret;

		if (qat_uclo_map_auth_fw(handle,
					 (char *)simg_hdr[i].simg_buf,
					 (unsigned int)
					 simg_hdr[i].simg_len,
					 &desc))
			goto wr_err;
		if (qat_uclo_auth_fw(handle, desc))
			goto wr_err;
		if (qat_uclo_is_broadcast(handle, i)) {
			if (qat_uclo_broadcast_load_fw(handle, desc))
				goto wr_err;
		} else {
			if (qat_uclo_load_fw(handle, desc))
				goto wr_err;
		}
		qat_uclo_ummap_auth_fw(handle, &desc);
	}
	return 0;
wr_err:
	qat_uclo_ummap_auth_fw(handle, &desc);
	return -EINVAL;
}

static int qat_uclo_wr_uof_img(struct icp_qat_fw_loader_handle *handle)
{
	struct icp_qat_uclo_objhandle *obj_handle = handle->obj_handle;
	unsigned int i;

	if (qat_uclo_init_globals(handle))
		return -EINVAL;
	for (i = 0; i < obj_handle->uimage_num; i++) {
		if (!obj_handle->ae_uimage[i].img_ptr)
			return -EINVAL;
		if (qat_uclo_init_ustore(handle, &obj_handle->ae_uimage[i]))
			return -EINVAL;
		qat_uclo_wr_uimage_page(handle,
					obj_handle->ae_uimage[i].img_ptr);
	}
	return 0;
}

int qat_uclo_wr_all_uimage(struct icp_qat_fw_loader_handle *handle)
{
	return (handle->chip_info->fw_auth) ? qat_uclo_wr_suof_img(handle) :
				   qat_uclo_wr_uof_img(handle);
}

int qat_uclo_set_cfg_ae_mask(struct icp_qat_fw_loader_handle *handle,
			     unsigned int cfg_ae_mask)
{
	if (!cfg_ae_mask)
		return -EINVAL;

	handle->cfg_ae_mask = cfg_ae_mask;
	return 0;
}
