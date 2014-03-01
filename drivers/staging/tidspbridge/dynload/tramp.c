/*
 * tramp.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "header.h"

#if TMS32060
#include "tramp_table_c6000.c"
#endif

#define MAX_RELOS_PER_PASS	4

/*
 * Function:	priv_tramp_sect_tgt_alloc
 * Description: Allocate target memory for the trampoline section.  The
 *	  target mem size is easily obtained as the next available address.
 */
static int priv_tramp_sect_tgt_alloc(struct dload_state *dlthis)
{
	int ret_val = 0;
	struct ldr_section_info *sect_info;

	/*  Populate the trampoline loader section and allocate it on the
	 * target.  The section name is ALWAYS the first string in the final
	 * string table for trampolines.  The trampoline section is always
	 * 1 beyond the total number of allocated sections. */
	sect_info = &dlthis->ldr_sections[dlthis->allocated_secn_count];

	sect_info->name = dlthis->tramp.final_string_table;
	sect_info->size = dlthis->tramp.tramp_sect_next_addr;
	sect_info->context = 0;
	sect_info->type =
	    (4 << 8) | DLOAD_TEXT | DS_ALLOCATE_MASK | DS_DOWNLOAD_MASK;
	sect_info->page = 0;
	sect_info->run_addr = 0;
	sect_info->load_addr = 0;
	ret_val = dlthis->myalloc->dload_allocate(dlthis->myalloc,
						  sect_info,
						  ds_alignment
						  (sect_info->type));

	if (ret_val == 0)
		dload_error(dlthis, "Failed to allocate target memory for"
			    " trampoline");

	return ret_val;
}

/*
 * Function:	priv_h2a
 * Description: Helper function to convert a hex value to its ASCII
 *	  representation.  Used for trampoline symbol name generation.
 */
static u8 priv_h2a(u8 value)
{
	if (value > 0xF)
		return 0xFF;

	if (value <= 9)
		value += 0x30;
	else
		value += 0x37;

	return value;
}

/*
 * Function:	priv_tramp_sym_gen_name
 * Description: Generate a trampoline symbol name (ASCII) using the value
 *	  of the symbol.  This places the new name into the user buffer.
 *	  The name is fixed in length and of the form: __$dbTR__xxxxxxxx
 *	  (where "xxxxxxxx" is the hex value).
 */
static void priv_tramp_sym_gen_name(u32 value, char *dst)
{
	u32 i;
	char *prefix = TRAMP_SYM_PREFIX;
	char *dst_local = dst;
	u8 tmp;

	/*  Clear out the destination, including the ending NULL */
	for (i = 0; i < (TRAMP_SYM_PREFIX_LEN + TRAMP_SYM_HEX_ASCII_LEN); i++)
		*(dst_local + i) = 0;

	/*  Copy the prefix to start */
	for (i = 0; i < strlen(TRAMP_SYM_PREFIX); i++) {
		*dst_local = *(prefix + i);
		dst_local++;
	}

	/*  Now convert the value passed in to a string equiv of the hex */
	for (i = 0; i < sizeof(value); i++) {
#ifndef _BIG_ENDIAN
		tmp = *(((u8 *) &value) + (sizeof(value) - 1) - i);
		*dst_local = priv_h2a((tmp & 0xF0) >> 4);
		dst_local++;
		*dst_local = priv_h2a(tmp & 0x0F);
		dst_local++;
#else
		tmp = *(((u8 *) &value) + i);
		*dst_local = priv_h2a((tmp & 0xF0) >> 4);
		dst_local++;
		*dst_local = priv_h2a(tmp & 0x0F);
		dst_local++;
#endif
	}

	/*  NULL terminate */
	*dst_local = 0;
}

/*
 * Function:	priv_tramp_string_create
 * Description: Create a new string specific to the trampoline loading and add
 *	  it to the trampoline string list.  This list contains the
 *	  trampoline section name and trampoline point symbols.
 */
static struct tramp_string *priv_tramp_string_create(struct dload_state *dlthis,
						     u32 str_len, char *str)
{
	struct tramp_string *new_string = NULL;
	u32 i;

	/*  Create a new string object with the specified size. */
	new_string =
	    (struct tramp_string *)dlthis->mysym->dload_allocate(dlthis->mysym,
								 (sizeof
								  (struct
								   tramp_string)
								  + str_len +
								  1));
	if (new_string != NULL) {
		/*  Clear the string first.  This ensures the ending NULL is
		 * present and the optimizer won't touch it. */
		for (i = 0; i < (sizeof(struct tramp_string) + str_len + 1);
		     i++)
			*((u8 *) new_string + i) = 0;

		/*  Add this string to our virtual table by assigning it the
		 * next index and pushing it to the tail of the list. */
		new_string->index = dlthis->tramp.tramp_string_next_index;
		dlthis->tramp.tramp_string_next_index++;
		dlthis->tramp.tramp_string_size += str_len + 1;

		new_string->next = NULL;
		if (dlthis->tramp.string_head == NULL)
			dlthis->tramp.string_head = new_string;
		else
			dlthis->tramp.string_tail->next = new_string;

		dlthis->tramp.string_tail = new_string;

		/*  Copy the string over to the new object */
		for (i = 0; i < str_len; i++)
			new_string->str[i] = str[i];
	}

	return new_string;
}

/*
 * Function:	priv_tramp_string_find
 * Description: Walk the trampoline string list and find a match for the
 *	  provided string.  If not match is found, NULL is returned.
 */
static struct tramp_string *priv_tramp_string_find(struct dload_state *dlthis,
						   char *str)
{
	struct tramp_string *cur_str = NULL;
	struct tramp_string *ret_val = NULL;
	u32 i;
	u32 str_len = strlen(str);

	for (cur_str = dlthis->tramp.string_head;
	     (ret_val == NULL) && (cur_str != NULL); cur_str = cur_str->next) {
		/*  If the string lengths aren't equal, don't bother
		 * comparing */
		if (str_len != strlen(cur_str->str))
			continue;

		/*  Walk the strings until one of them ends */
		for (i = 0; i < str_len; i++) {
			/*  If they don't match in the current position then
			 * break out now, no sense in continuing to look at
			 * this string. */
			if (str[i] != cur_str->str[i])
				break;
		}

		if (i == str_len)
			ret_val = cur_str;
	}

	return ret_val;
}

/*
 * Function:	priv_string_tbl_finalize
 * Description: Flatten the trampoline string list into a table of NULL
 *	  terminated strings.  This is the same format of string table
 *	  as used by the COFF/DOFF file.
 */
static int priv_string_tbl_finalize(struct dload_state *dlthis)
{
	int ret_val = 0;
	struct tramp_string *cur_string;
	char *cur_loc;
	char *tmp;

	/*  Allocate enough space for all strings that have been created.  The
	 * table is simply all strings concatenated together will NULL
	 * endings. */
	dlthis->tramp.final_string_table =
	    (char *)dlthis->mysym->dload_allocate(dlthis->mysym,
						  dlthis->tramp.
						  tramp_string_size);
	if (dlthis->tramp.final_string_table != NULL) {
		/*  We got our buffer, walk the list and release the nodes as*
		 * we go */
		cur_loc = dlthis->tramp.final_string_table;
		cur_string = dlthis->tramp.string_head;
		while (cur_string != NULL) {
			/*  Move the head/tail pointers */
			dlthis->tramp.string_head = cur_string->next;
			if (dlthis->tramp.string_tail == cur_string)
				dlthis->tramp.string_tail = NULL;

			/*  Copy the string contents */
			for (tmp = cur_string->str;
			     *tmp != '\0'; tmp++, cur_loc++)
				*cur_loc = *tmp;

			/*  Pick up the NULL termination since it was missed by
			 * breaking using it to end the above loop. */
			*cur_loc = '\0';
			cur_loc++;

			/*  Free the string node, we don't need it any more. */
			dlthis->mysym->dload_deallocate(dlthis->mysym,
							cur_string);

			/*  Move our pointer to the next one */
			cur_string = dlthis->tramp.string_head;
		}

		/*  Update our return value to success */
		ret_val = 1;
	} else
		dload_error(dlthis, "Failed to allocate trampoline "
			    "string table");

	return ret_val;
}

/*
 * Function:	priv_tramp_sect_alloc
 * Description: Virtually allocate space from the trampoline section.  This
 *	  function returns the next offset within the trampoline section
 *	  that is available and moved the next available offset by the
 *	  requested size.  NO TARGET ALLOCATION IS DONE AT THIS TIME.
 */
static u32 priv_tramp_sect_alloc(struct dload_state *dlthis, u32 tramp_size)
{
	u32 ret_val;

	/*  If the next available address is 0, this is our first allocation.
	 * Create a section name string to go into the string table . */
	if (dlthis->tramp.tramp_sect_next_addr == 0) {
		dload_syms_error(dlthis->mysym, "*** WARNING ***  created "
				 "dynamic TRAMPOLINE section for module %s",
				 dlthis->str_head);
	}

	/*  Reserve space for the new trampoline */
	ret_val = dlthis->tramp.tramp_sect_next_addr;
	dlthis->tramp.tramp_sect_next_addr += tramp_size;
	return ret_val;
}

/*
 * Function:	priv_tramp_sym_create
 * Description: Allocate and create a new trampoline specific symbol and add
 *	  it to the trampoline symbol list.  These symbols will include
 *	  trampoline points as well as the external symbols they
 *	  reference.
 */
static struct tramp_sym *priv_tramp_sym_create(struct dload_state *dlthis,
					       u32 str_index,
					       struct local_symbol *tmp_sym)
{
	struct tramp_sym *new_sym = NULL;
	u32 i;

	/*  Allocate new space for the symbol in the symbol table. */
	new_sym =
	    (struct tramp_sym *)dlthis->mysym->dload_allocate(dlthis->mysym,
					      sizeof(struct tramp_sym));
	if (new_sym != NULL) {
		for (i = 0; i != sizeof(struct tramp_sym); i++)
			*((char *)new_sym + i) = 0;

		/*  Assign this symbol the next symbol index for easier
		 * reference later during relocation. */
		new_sym->index = dlthis->tramp.tramp_sym_next_index;
		dlthis->tramp.tramp_sym_next_index++;

		/*  Populate the symbol information.  At this point any
		 * trampoline symbols will be the offset location, not the
		 * final.  Copy over the symbol info to start, then be sure to
		 * get the string index from the trampoline string table. */
		new_sym->sym_info = *tmp_sym;
		new_sym->str_index = str_index;

		/*  Push the new symbol to the tail of the symbol table list */
		new_sym->next = NULL;
		if (dlthis->tramp.symbol_head == NULL)
			dlthis->tramp.symbol_head = new_sym;
		else
			dlthis->tramp.symbol_tail->next = new_sym;

		dlthis->tramp.symbol_tail = new_sym;
	}

	return new_sym;
}

/*
 * Function:	priv_tramp_sym_get
 * Description: Search for the symbol with the matching string index (from
 *	  the trampoline string table) and return the trampoline
 *	  symbol object, if found.  Otherwise return NULL.
 */
static struct tramp_sym *priv_tramp_sym_get(struct dload_state *dlthis,
					    u32 string_index)
{
	struct tramp_sym *sym_found = NULL;

	/*  Walk the symbol table list and search vs. the string index */
	for (sym_found = dlthis->tramp.symbol_head;
	     sym_found != NULL; sym_found = sym_found->next) {
		if (sym_found->str_index == string_index)
			break;
	}

	return sym_found;
}

/*
 * Function:	priv_tramp_sym_find
 * Description: Search for a trampoline symbol based on the string name of
 *	  the symbol.  Return the symbol object, if found, otherwise
 *	  return NULL.
 */
static struct tramp_sym *priv_tramp_sym_find(struct dload_state *dlthis,
					     char *string)
{
	struct tramp_sym *sym_found = NULL;
	struct tramp_string *str_found = NULL;

	/*  First, search for the string, then search for the sym based on the
	   string index. */
	str_found = priv_tramp_string_find(dlthis, string);
	if (str_found != NULL)
		sym_found = priv_tramp_sym_get(dlthis, str_found->index);

	return sym_found;
}

/*
 * Function:	priv_tramp_sym_finalize
 * Description: Allocate a flat symbol table for the trampoline section,
 *	  put each trampoline symbol into the table, adjust the
 *	  symbol value based on the section address on the target and
 *	  free the trampoline symbol list nodes.
 */
static int priv_tramp_sym_finalize(struct dload_state *dlthis)
{
	int ret_val = 0;
	struct tramp_sym *cur_sym;
	struct ldr_section_info *tramp_sect =
	    &dlthis->ldr_sections[dlthis->allocated_secn_count];
	struct local_symbol *new_sym;

	/*  Allocate a table to hold a flattened version of all symbols
	 * created. */
	dlthis->tramp.final_sym_table =
	    (struct local_symbol *)dlthis->mysym->dload_allocate(dlthis->mysym,
				 (sizeof(struct local_symbol) * dlthis->tramp.
						  tramp_sym_next_index));
	if (dlthis->tramp.final_sym_table != NULL) {
		/*  Walk the list of all symbols, copy it over to the flattened
		 * table. After it has been copied, the node can be freed as
		 * it is no longer needed. */
		new_sym = dlthis->tramp.final_sym_table;
		cur_sym = dlthis->tramp.symbol_head;
		while (cur_sym != NULL) {
			/*  Pop it off the list */
			dlthis->tramp.symbol_head = cur_sym->next;
			if (cur_sym == dlthis->tramp.symbol_tail)
				dlthis->tramp.symbol_tail = NULL;

			/*  Copy the symbol contents into the flat table */
			*new_sym = cur_sym->sym_info;

			/*  Now finalize the symbol.  If it is in the tramp
			 * section, we need to adjust for the section start.
			 * If it is external then we don't need to adjust at
			 * all.
			 * NOTE: THIS CODE ASSUMES THAT THE TRAMPOLINE IS
			 * REFERENCED LIKE A CALL TO AN EXTERNAL SO VALUE AND
			 * DELTA ARE THE SAME.  SEE THE FUNCTION dload_symbols
			 * WHERE DN_UNDEF IS HANDLED FOR MORE REFERENCE. */
			if (new_sym->secnn < 0) {
				new_sym->value += tramp_sect->load_addr;
				new_sym->delta = new_sym->value;
			}

			/*  Let go of the symbol node */
			dlthis->mysym->dload_deallocate(dlthis->mysym, cur_sym);

			/*  Move to the next node */
			cur_sym = dlthis->tramp.symbol_head;
			new_sym++;
		}

		ret_val = 1;
	} else
		dload_error(dlthis, "Failed to alloc trampoline sym table");

	return ret_val;
}

/*
 * Function:	priv_tgt_img_gen
 * Description: Allocate storage for and copy the target specific image data
 *	and fix up its relocations for the new external symbol.  If
 *	a trampoline image packet was successfully created it is added
 *	to the trampoline list.
 */
static int priv_tgt_img_gen(struct dload_state *dlthis, u32 base,
			    u32 gen_index, struct tramp_sym *new_ext_sym)
{
	struct tramp_img_pkt *new_img_pkt = NULL;
	u32 i;
	u32 pkt_size = tramp_img_pkt_size_get();
	u8 *gen_tbl_entry;
	u8 *pkt_data;
	struct reloc_record_t *cur_relo;
	int ret_val = 0;

	/*  Allocate a new image packet and set it up. */
	new_img_pkt =
	    (struct tramp_img_pkt *)dlthis->mysym->dload_allocate(dlthis->mysym,
								  pkt_size);
	if (new_img_pkt != NULL) {
		/*  Save the base, this is where it goes in the section */
		new_img_pkt->base = base;

		/*  Copy over the image data and relos from the target table */
		pkt_data = (u8 *) &new_img_pkt->hdr;
		gen_tbl_entry = (u8 *) &tramp_gen_info[gen_index];
		for (i = 0; i < pkt_size; i++) {
			*pkt_data = *gen_tbl_entry;
			pkt_data++;
			gen_tbl_entry++;
		}

		/*  Update the relocations to point to the external symbol */
		cur_relo =
		    (struct reloc_record_t *)((u8 *) &new_img_pkt->hdr +
					      new_img_pkt->hdr.relo_offset);
		for (i = 0; i < new_img_pkt->hdr.num_relos; i++)
			cur_relo[i].SYMNDX = new_ext_sym->index;

		/*  Add it to the trampoline list. */
		new_img_pkt->next = dlthis->tramp.tramp_pkts;
		dlthis->tramp.tramp_pkts = new_img_pkt;

		ret_val = 1;
	}

	return ret_val;
}

/*
 * Function:	priv_pkt_relo
 * Description: Take the provided image data and the collection of relocations
 *	  for it and perform the relocations.  Note that all relocations
 *	  at this stage are considered SECOND PASS since the original
 *	  image has already been processed in the first pass.  This means
 *	  TRAMPOLINES ARE TREATED AS 2ND PASS even though this is really
 *	  the first (and only) relocation that will be performed on them.
 */
static int priv_pkt_relo(struct dload_state *dlthis, tgt_au_t *data,
			 struct reloc_record_t *rp[], u32 relo_count)
{
	int ret_val = 1;
	u32 i;
	bool tmp;

	/*  Walk through all of the relos and process them.  This function is
	 * the equivalent of relocate_packet() from cload.c, but specialized
	 * for trampolines and 2nd phase relocations. */
	for (i = 0; i < relo_count; i++)
		dload_relocate(dlthis, data, rp[i], &tmp, true);

	return ret_val;
}

/*
 * Function:	priv_tramp_pkt_finalize
 * Description: Walk the list of all trampoline packets and finalize them.
 *	  Each trampoline image packet will be relocated now that the
 *	  trampoline section has been allocated on the target.  Once
 *	  all of the relocations are done the trampoline image data
 *	  is written into target memory and the trampoline packet
 *	  is freed: it is no longer needed after this point.
 */
static int priv_tramp_pkt_finalize(struct dload_state *dlthis)
{
	int ret_val = 1;
	struct tramp_img_pkt *cur_pkt = NULL;
	struct reloc_record_t *relos[MAX_RELOS_PER_PASS];
	u32 relos_done;
	u32 i;
	struct reloc_record_t *cur_relo;
	struct ldr_section_info *sect_info =
	    &dlthis->ldr_sections[dlthis->allocated_secn_count];

	/*  Walk the list of trampoline packets and relocate each packet.  This
	 * function is the trampoline equivalent of dload_data() from
	 * cload.c. */
	cur_pkt = dlthis->tramp.tramp_pkts;
	while ((ret_val != 0) && (cur_pkt != NULL)) {
		/*  Remove the pkt from the list */
		dlthis->tramp.tramp_pkts = cur_pkt->next;

		/*  Setup section and image offset information for the relo */
		dlthis->image_secn = sect_info;
		dlthis->image_offset = cur_pkt->base;
		dlthis->delta_runaddr = sect_info->run_addr;

		/*  Walk through all relos for the packet */
		relos_done = 0;
		cur_relo = (struct reloc_record_t *)((u8 *) &cur_pkt->hdr +
						     cur_pkt->hdr.relo_offset);
		while (relos_done < cur_pkt->hdr.num_relos) {
#ifdef ENABLE_TRAMP_DEBUG
			dload_syms_error(dlthis->mysym,
					 "===> Trampoline %x branches to %x",
					 sect_info->run_addr +
					 dlthis->image_offset,
					 dlthis->
					 tramp.final_sym_table[cur_relo->
							       SYMNDX].value);
#endif

			for (i = 0;
			     ((i < MAX_RELOS_PER_PASS) &&
			      ((i + relos_done) < cur_pkt->hdr.num_relos)); i++)
				relos[i] = cur_relo + i;

			/*  Do the actual relo */
			ret_val = priv_pkt_relo(dlthis,
						(tgt_au_t *) &cur_pkt->payload,
						relos, i);
			if (ret_val == 0) {
				dload_error(dlthis,
					    "Relocation of trampoline pkt at %x"
					    " failed", cur_pkt->base +
					    sect_info->run_addr);
				break;
			}

			relos_done += i;
			cur_relo += i;
		}

		/*  Make sure we didn't hit a problem */
		if (ret_val != 0) {
			/*  Relos are done for the packet, write it to the
			 * target */
			ret_val = dlthis->myio->writemem(dlthis->myio,
							 &cur_pkt->payload,
							 sect_info->load_addr +
							 cur_pkt->base,
							 sect_info,
							 BYTE_TO_HOST
							 (cur_pkt->hdr.
							  tramp_code_size));
			if (ret_val == 0) {
				dload_error(dlthis,
					    "Write to " FMT_UI32 " failed",
					    sect_info->load_addr +
					    cur_pkt->base);
			}

			/*  Done with the pkt, let it go */
			dlthis->mysym->dload_deallocate(dlthis->mysym, cur_pkt);

			/*  Get the next packet to process */
			cur_pkt = dlthis->tramp.tramp_pkts;
		}
	}

	return ret_val;
}

/*
 * Function:	priv_dup_pkt_finalize
 * Description: Walk the list of duplicate image packets and finalize them.
 *	  Each duplicate packet will be relocated again for the
 *	  relocations that previously failed and have been adjusted
 *	  to point at a trampoline.  Once all relocations for a packet
 *	  have been done, write the packet into target memory.  The
 *	  duplicate packet and its relocation chain are all freed
 *	  after use here as they are no longer needed after this.
 */
static int priv_dup_pkt_finalize(struct dload_state *dlthis)
{
	int ret_val = 1;
	struct tramp_img_dup_pkt *cur_pkt;
	struct tramp_img_dup_relo *cur_relo;
	struct reloc_record_t *relos[MAX_RELOS_PER_PASS];
	struct doff_scnhdr_t *sect_hdr = NULL;
	s32 i;

	/* Similar to the trampoline pkt finalize, this function walks each dup
	 * pkt that was generated and performs all relocations that were
	 * deferred to a 2nd pass.  This is the equivalent of dload_data() from
	 * cload.c, but does not need the additional reorder and checksum
	 * processing as it has already been done. */
	cur_pkt = dlthis->tramp.dup_pkts;
	while ((ret_val != 0) && (cur_pkt != NULL)) {
		/*  Remove the node from the list, we'll be freeing it
		 * shortly */
		dlthis->tramp.dup_pkts = cur_pkt->next;

		/*  Setup the section and image offset for relocation */
		dlthis->image_secn = &dlthis->ldr_sections[cur_pkt->secnn];
		dlthis->image_offset = cur_pkt->offset;

		/*  In order to get the delta run address, we need to reference
		 * the original section header.  It's a bit ugly, but needed
		 * for relo. */
		i = (s32) (dlthis->image_secn - dlthis->ldr_sections);
		sect_hdr = dlthis->sect_hdrs + i;
		dlthis->delta_runaddr = sect_hdr->ds_paddr;

		/*  Walk all relos in the chain and process each. */
		cur_relo = cur_pkt->relo_chain;
		while (cur_relo != NULL) {
			/*  Process them a chunk at a time to be efficient */
			for (i = 0; (i < MAX_RELOS_PER_PASS)
			     && (cur_relo != NULL);
			     i++, cur_relo = cur_relo->next) {
				relos[i] = &cur_relo->relo;
				cur_pkt->relo_chain = cur_relo->next;
			}

			/*  Do the actual relo */
			ret_val = priv_pkt_relo(dlthis,
						cur_pkt->img_pkt.img_data,
						relos, i);
			if (ret_val == 0) {
				dload_error(dlthis,
					    "Relocation of dup pkt at %x"
					    " failed", cur_pkt->offset +
					    dlthis->image_secn->run_addr);
				break;
			}

			/*  Release all of these relos, we're done with them */
			while (i > 0) {
				dlthis->mysym->dload_deallocate(dlthis->mysym,
						GET_CONTAINER
						(relos[i - 1],
						 struct tramp_img_dup_relo,
						 relo));
				i--;
			}

			/*  DO NOT ADVANCE cur_relo, IT IS ALREADY READY TO
			 * GO! */
		}

		/* Done with all relos.  Make sure we didn't have a problem and
		 * write it out to the target */
		if (ret_val != 0) {
			ret_val = dlthis->myio->writemem(dlthis->myio,
							 cur_pkt->img_pkt.
							 img_data,
							 dlthis->image_secn->
							 load_addr +
							 cur_pkt->offset,
							 dlthis->image_secn,
							 BYTE_TO_HOST
							 (cur_pkt->img_pkt.
							  packet_size));
			if (ret_val == 0) {
				dload_error(dlthis,
					    "Write to " FMT_UI32 " failed",
					    dlthis->image_secn->load_addr +
					    cur_pkt->offset);
			}

			dlthis->mysym->dload_deallocate(dlthis->mysym, cur_pkt);

			/*  Advance to the next packet */
			cur_pkt = dlthis->tramp.dup_pkts;
		}
	}

	return ret_val;
}

/*
 * Function:	priv_dup_find
 * Description: Walk the list of existing duplicate packets and find a
 *	  match based on the section number and image offset.  Return
 *	  the duplicate packet if found, otherwise NULL.
 */
static struct tramp_img_dup_pkt *priv_dup_find(struct dload_state *dlthis,
					       s16 secnn, u32 image_offset)
{
	struct tramp_img_dup_pkt *cur_pkt = NULL;

	for (cur_pkt = dlthis->tramp.dup_pkts;
	     cur_pkt != NULL; cur_pkt = cur_pkt->next) {
		if ((cur_pkt->secnn == secnn) &&
		    (cur_pkt->offset == image_offset)) {
			/*  Found a match, break out */
			break;
		}
	}

	return cur_pkt;
}

/*
 * Function:	priv_img_pkt_dup
 * Description: Duplicate the original image packet.  If this is the first
 *	  time this image packet has been seen (based on section number
 *	  and image offset), create a new duplicate packet and add it
 *	  to the dup packet list.  If not, just get the existing one and
 *	  update it with the current packet contents (since relocation
 *	  on the packet is still ongoing in first pass.)  Create a
 *	  duplicate of the provided relocation, but update it to point
 *	  to the new trampoline symbol.  Add the new relocation dup to
 *	  the dup packet's relo chain for 2nd pass relocation later.
 */
static int priv_img_pkt_dup(struct dload_state *dlthis,
			    s16 secnn, u32 image_offset,
			    struct image_packet_t *ipacket,
			    struct reloc_record_t *rp,
			    struct tramp_sym *new_tramp_sym)
{
	struct tramp_img_dup_pkt *dup_pkt = NULL;
	u32 new_dup_size;
	s32 i;
	int ret_val = 0;
	struct tramp_img_dup_relo *dup_relo = NULL;

	/*  Determine if this image packet is already being tracked in the
	   dup list for other trampolines. */
	dup_pkt = priv_dup_find(dlthis, secnn, image_offset);

	if (dup_pkt == NULL) {
		/*  This image packet does not exist in our tracking, so create
		 * a new one and add it to the head of the list. */
		new_dup_size = sizeof(struct tramp_img_dup_pkt) +
		    ipacket->packet_size;

		dup_pkt = (struct tramp_img_dup_pkt *)
		    dlthis->mysym->dload_allocate(dlthis->mysym, new_dup_size);
		if (dup_pkt != NULL) {
			/*  Save off the section and offset information */
			dup_pkt->secnn = secnn;
			dup_pkt->offset = image_offset;
			dup_pkt->relo_chain = NULL;

			/*  Copy the original packet content */
			dup_pkt->img_pkt = *ipacket;
			dup_pkt->img_pkt.img_data = (u8 *) (dup_pkt + 1);
			for (i = 0; i < ipacket->packet_size; i++)
				*(dup_pkt->img_pkt.img_data + i) =
				    *(ipacket->img_data + i);

			/*  Add the packet to the dup list */
			dup_pkt->next = dlthis->tramp.dup_pkts;
			dlthis->tramp.dup_pkts = dup_pkt;
		} else
			dload_error(dlthis, "Failed to create dup packet!");
	} else {
		/*  The image packet contents could have changed since
		 * trampoline detection happens during relocation of the image
		 * packets.  So, we need to update the image packet contents
		 * before adding relo information. */
		for (i = 0; i < dup_pkt->img_pkt.packet_size; i++)
			*(dup_pkt->img_pkt.img_data + i) =
			    *(ipacket->img_data + i);
	}

	/*  Since the previous code may have allocated a new dup packet for us,
	   double check that we actually have one. */
	if (dup_pkt != NULL) {
		/*  Allocate a new node for the relo chain.  Each image packet
		 * can potentially have multiple relocations that cause a
		 * trampoline to be generated.  So, we keep them in a chain,
		 * order is not important. */
		dup_relo = dlthis->mysym->dload_allocate(dlthis->mysym,
					 sizeof(struct tramp_img_dup_relo));
		if (dup_relo != NULL) {
			/*  Copy the relo contents, adjust for the new
			 * trampoline and add it to the list. */
			dup_relo->relo = *rp;
			dup_relo->relo.SYMNDX = new_tramp_sym->index;

			dup_relo->next = dup_pkt->relo_chain;
			dup_pkt->relo_chain = dup_relo;

			/*  That's it, we're done.  Make sure we update our
			 * return value to be success since everything finished
			 * ok */
			ret_val = 1;
		} else
			dload_error(dlthis, "Unable to alloc dup relo");
	}

	return ret_val;
}

/*
 * Function:	dload_tramp_avail
 * Description: Check to see if the target supports a trampoline for this type
 *	  of relocation.  Return true if it does, otherwise false.
 */
bool dload_tramp_avail(struct dload_state *dlthis, struct reloc_record_t *rp)
{
	bool ret_val = false;
	u16 map_index;
	u16 gen_index;

	/*  Check type hash vs. target tramp table */
	map_index = HASH_FUNC(rp->TYPE);
	gen_index = tramp_map[map_index];
	if (gen_index != TRAMP_NO_GEN_AVAIL)
		ret_val = true;

	return ret_val;
}

/*
 * Function:	dload_tramp_generate
 * Description: Create a new trampoline for the provided image packet and
 *	  relocation causing problems.  This will create the trampoline
 *	  as well as duplicate/update the image packet and relocation
 *	  causing the problem, which will be relo'd again during
 *	  finalization.
 */
int dload_tramp_generate(struct dload_state *dlthis, s16 secnn,
			 u32 image_offset, struct image_packet_t *ipacket,
			 struct reloc_record_t *rp)
{
	u16 map_index;
	u16 gen_index;
	int ret_val = 1;
	char tramp_sym_str[TRAMP_SYM_PREFIX_LEN + TRAMP_SYM_HEX_ASCII_LEN];
	struct local_symbol *ref_sym;
	struct tramp_sym *new_tramp_sym;
	struct tramp_sym *new_ext_sym;
	struct tramp_string *new_tramp_str;
	u32 new_tramp_base;
	struct local_symbol tmp_sym;
	struct local_symbol ext_tmp_sym;

	/*  Hash the relo type to get our generator information */
	map_index = HASH_FUNC(rp->TYPE);
	gen_index = tramp_map[map_index];
	if (gen_index != TRAMP_NO_GEN_AVAIL) {
		/*  If this is the first trampoline, create the section name in
		 * our string table for debug help later. */
		if (dlthis->tramp.string_head == NULL) {
			priv_tramp_string_create(dlthis,
						 strlen(TRAMP_SECT_NAME),
						 TRAMP_SECT_NAME);
		}
#ifdef ENABLE_TRAMP_DEBUG
		dload_syms_error(dlthis->mysym,
				 "Trampoline at img loc %x, references %x",
				 dlthis->ldr_sections[secnn].run_addr +
				 image_offset + rp->vaddr,
				 dlthis->local_symtab[rp->SYMNDX].value);
#endif

		/*  Generate the trampoline string, check if already defined.
		 * If the relo symbol index is -1, it means we need the section
		 * info for relo later.  To do this we'll dummy up a symbol
		 * with the section delta and run addresses. */
		if (rp->SYMNDX == -1) {
			ext_tmp_sym.value =
			    dlthis->ldr_sections[secnn].run_addr;
			ext_tmp_sym.delta = dlthis->sect_hdrs[secnn].ds_paddr;
			ref_sym = &ext_tmp_sym;
		} else
			ref_sym = &(dlthis->local_symtab[rp->SYMNDX]);

		priv_tramp_sym_gen_name(ref_sym->value, tramp_sym_str);
		new_tramp_sym = priv_tramp_sym_find(dlthis, tramp_sym_str);
		if (new_tramp_sym == NULL) {
			/*  If tramp string not defined, create it and a new
			 * string, and symbol for it as well as the original
			 * symbol which caused the trampoline. */
			new_tramp_str = priv_tramp_string_create(dlthis,
								strlen
								(tramp_sym_str),
								 tramp_sym_str);
			if (new_tramp_str == NULL) {
				dload_error(dlthis, "Failed to create new "
					    "trampoline string\n");
				ret_val = 0;
			} else {
				/*  Allocate tramp section space for the new
				 * tramp from the target */
				new_tramp_base = priv_tramp_sect_alloc(dlthis,
						       tramp_size_get());

				/*  We have a string, create the new symbol and
				 * duplicate the external. */
				tmp_sym.value = new_tramp_base;
				tmp_sym.delta = 0;
				tmp_sym.secnn = -1;
				tmp_sym.sclass = 0;
				new_tramp_sym = priv_tramp_sym_create(dlthis,
							      new_tramp_str->
							      index,
							      &tmp_sym);

				new_ext_sym = priv_tramp_sym_create(dlthis, -1,
								    ref_sym);

				if ((new_tramp_sym != NULL) &&
				    (new_ext_sym != NULL)) {
					/*  Call the image generator to get the
					 * new image data and fix up its
					 * relocations for the external
					 * symbol. */
					ret_val = priv_tgt_img_gen(dlthis,
								 new_tramp_base,
								 gen_index,
								 new_ext_sym);

					/*  Add generated image data to tramp
					 * image list */
					if (ret_val != 1) {
						dload_error(dlthis, "Failed to "
							    "create img pkt for"
							    " trampoline\n");
					}
				} else {
					dload_error(dlthis, "Failed to create "
						    "new tramp syms "
						    "(%8.8X, %8.8X)\n",
						    new_tramp_sym, new_ext_sym);
					ret_val = 0;
				}
			}
		}

		/*  Duplicate the image data and relo record that caused the
		 * tramp, including update the relo data to point to the tramp
		 * symbol. */
		if (ret_val == 1) {
			ret_val = priv_img_pkt_dup(dlthis, secnn, image_offset,
						   ipacket, rp, new_tramp_sym);
			if (ret_val != 1) {
				dload_error(dlthis, "Failed to create dup of "
					    "original img pkt\n");
			}
		}
	}

	return ret_val;
}

/*
 * Function:	dload_tramp_pkt_update
 * Description: Update the duplicate copy of this image packet, which the
 *	  trampoline layer is already tracking.  This call is critical
 *	  to make if trampolines were generated anywhere within the
 *	  packet and first pass relo continued on the remainder.  The
 *	  trampoline layer needs the updates image data so when 2nd
 *	  pass relo is done during finalize the image packet can be
 *	  written to the target since all relo is done.
 */
int dload_tramp_pkt_udpate(struct dload_state *dlthis, s16 secnn,
			   u32 image_offset, struct image_packet_t *ipacket)
{
	struct tramp_img_dup_pkt *dup_pkt = NULL;
	s32 i;
	int ret_val = 0;

	/*  Find the image packet in question, the caller needs us to update it
	   since a trampoline was previously generated. */
	dup_pkt = priv_dup_find(dlthis, secnn, image_offset);
	if (dup_pkt != NULL) {
		for (i = 0; i < dup_pkt->img_pkt.packet_size; i++)
			*(dup_pkt->img_pkt.img_data + i) =
			    *(ipacket->img_data + i);

		ret_val = 1;
	} else {
		dload_error(dlthis,
			    "Unable to find existing DUP pkt for %x, offset %x",
			    secnn, image_offset);

	}

	return ret_val;
}

/*
 * Function:	dload_tramp_finalize
 * Description: If any trampolines were created, finalize everything on the
 *	  target by allocating the trampoline section on the target,
 *	  finalizing the trampoline symbols, finalizing the trampoline
 *	  packets (write the new section to target memory) and finalize
 *	  the duplicate packets by doing 2nd pass relo over them.
 */
int dload_tramp_finalize(struct dload_state *dlthis)
{
	int ret_val = 1;

	if (dlthis->tramp.tramp_sect_next_addr != 0) {
		/*  Finalize strings into a flat table.  This is needed so it
		 * can be added to the debug string table later. */
		ret_val = priv_string_tbl_finalize(dlthis);

		/*  Do target allocation for section BEFORE finalizing
		 * symbols. */
		if (ret_val != 0)
			ret_val = priv_tramp_sect_tgt_alloc(dlthis);

		/*  Finalize symbols with their correct target information and
		 * flatten */
		if (ret_val != 0)
			ret_val = priv_tramp_sym_finalize(dlthis);

		/*  Finalize all trampoline packets.  This performs the
		 * relocation on the packets as well as writing them to target
		 * memory. */
		if (ret_val != 0)
			ret_val = priv_tramp_pkt_finalize(dlthis);

		/*  Perform a 2nd pass relocation on the dup list. */
		if (ret_val != 0)
			ret_val = priv_dup_pkt_finalize(dlthis);
	}

	return ret_val;
}

/*
 * Function:	dload_tramp_cleanup
 * Description: Release all temporary resources used in the trampoline layer.
 *	  Note that the target memory which may have been allocated and
 *	  written to store the trampolines is NOT RELEASED HERE since it
 *	  is potentially still in use.  It is automatically released
 *	  when the module is unloaded.
 */
void dload_tramp_cleanup(struct dload_state *dlthis)
{
	struct tramp_info *tramp = &dlthis->tramp;
	struct tramp_sym *cur_sym;
	struct tramp_string *cur_string;
	struct tramp_img_pkt *cur_tramp_pkt;
	struct tramp_img_dup_pkt *cur_dup_pkt;
	struct tramp_img_dup_relo *cur_dup_relo;

	/*  If there were no tramps generated, just return */
	if (tramp->tramp_sect_next_addr == 0)
		return;

	/*  Destroy all tramp information */
	for (cur_sym = tramp->symbol_head;
	     cur_sym != NULL; cur_sym = tramp->symbol_head) {
		tramp->symbol_head = cur_sym->next;
		if (tramp->symbol_tail == cur_sym)
			tramp->symbol_tail = NULL;

		dlthis->mysym->dload_deallocate(dlthis->mysym, cur_sym);
	}

	if (tramp->final_sym_table != NULL)
		dlthis->mysym->dload_deallocate(dlthis->mysym,
						tramp->final_sym_table);

	for (cur_string = tramp->string_head;
	     cur_string != NULL; cur_string = tramp->string_head) {
		tramp->string_head = cur_string->next;
		if (tramp->string_tail == cur_string)
			tramp->string_tail = NULL;

		dlthis->mysym->dload_deallocate(dlthis->mysym, cur_string);
	}

	if (tramp->final_string_table != NULL)
		dlthis->mysym->dload_deallocate(dlthis->mysym,
						tramp->final_string_table);

	for (cur_tramp_pkt = tramp->tramp_pkts;
	     cur_tramp_pkt != NULL; cur_tramp_pkt = tramp->tramp_pkts) {
		tramp->tramp_pkts = cur_tramp_pkt->next;
		dlthis->mysym->dload_deallocate(dlthis->mysym, cur_tramp_pkt);
	}

	for (cur_dup_pkt = tramp->dup_pkts;
	     cur_dup_pkt != NULL; cur_dup_pkt = tramp->dup_pkts) {
		tramp->dup_pkts = cur_dup_pkt->next;

		for (cur_dup_relo = cur_dup_pkt->relo_chain;
		     cur_dup_relo != NULL;
		     cur_dup_relo = cur_dup_pkt->relo_chain) {
			cur_dup_pkt->relo_chain = cur_dup_relo->next;
			dlthis->mysym->dload_deallocate(dlthis->mysym,
							cur_dup_relo);
		}

		dlthis->mysym->dload_deallocate(dlthis->mysym, cur_dup_pkt);
	}
}
