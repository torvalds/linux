/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <asm/byteorder.h>

#include "hinic_hw_if.h"
#include "hinic_hw_api_cmd.h"

#define API_CHAIN_NUM_CELLS                     32

#define API_CMD_CELL_SIZE_SHIFT                 6
#define API_CMD_CELL_SIZE_MIN                   (BIT(API_CMD_CELL_SIZE_SHIFT))

#define API_CMD_CELL_SIZE(cell_size)            \
		(((cell_size) >= API_CMD_CELL_SIZE_MIN) ? \
		 (1 << (fls(cell_size - 1))) : API_CMD_CELL_SIZE_MIN)

#define API_CMD_BUF_SIZE                        2048

/**
 * api_cmd_chain_hw_init - initialize the chain in the HW
 * @chain: the API CMD specific chain to initialize in HW
 *
 * Return 0 - Success, negative - Failure
 **/
static int api_cmd_chain_hw_init(struct hinic_api_cmd_chain *chain)
{
	/* should be implemented */
	return 0;
}

/**
 * free_cmd_buf - free the dma buffer of API CMD command
 * @chain: the API CMD specific chain of the cmd
 * @cell_idx: the cell index of the cmd
 **/
static void free_cmd_buf(struct hinic_api_cmd_chain *chain, int cell_idx)
{
	struct hinic_api_cmd_cell_ctxt *cell_ctxt;
	struct hinic_hwif *hwif = chain->hwif;
	struct pci_dev *pdev = hwif->pdev;

	cell_ctxt = &chain->cell_ctxt[cell_idx];

	dma_free_coherent(&pdev->dev, API_CMD_BUF_SIZE,
			  cell_ctxt->api_cmd_vaddr,
			  cell_ctxt->api_cmd_paddr);
}

/**
 * alloc_cmd_buf - allocate a dma buffer for API CMD command
 * @chain: the API CMD specific chain for the cmd
 * @cell: the cell in the HW for the cmd
 * @cell_idx: the index of the cell
 *
 * Return 0 - Success, negative - Failure
 **/
static int alloc_cmd_buf(struct hinic_api_cmd_chain *chain,
			 struct hinic_api_cmd_cell *cell, int cell_idx)
{
	struct hinic_api_cmd_cell_ctxt *cell_ctxt;
	struct hinic_hwif *hwif = chain->hwif;
	struct pci_dev *pdev = hwif->pdev;
	dma_addr_t cmd_paddr;
	u8 *cmd_vaddr;
	int err = 0;

	cmd_vaddr = dma_zalloc_coherent(&pdev->dev, API_CMD_BUF_SIZE,
					&cmd_paddr, GFP_KERNEL);
	if (!cmd_vaddr) {
		dev_err(&pdev->dev, "Failed to allocate API CMD DMA memory\n");
		return -ENOMEM;
	}

	cell_ctxt = &chain->cell_ctxt[cell_idx];

	cell_ctxt->api_cmd_vaddr = cmd_vaddr;
	cell_ctxt->api_cmd_paddr = cmd_paddr;

	/* set the cmd DMA address in the cell */
	switch (chain->chain_type) {
	case HINIC_API_CMD_WRITE_TO_MGMT_CPU:
		/* The data in the HW should be in Big Endian Format */
		cell->write.hw_cmd_paddr = cpu_to_be64(cmd_paddr);
		break;

	default:
		dev_err(&pdev->dev, "Unsupported API CMD chain type\n");
		free_cmd_buf(chain, cell_idx);
		err = -EINVAL;
		break;
	}

	return err;
}

/**
 * api_cmd_create_cell - create API CMD cell for specific chain
 * @chain: the API CMD specific chain to create its cell
 * @cell_idx: the index of the cell to create
 * @pre_node: previous cell
 * @node_vaddr: the returned virt addr of the cell
 *
 * Return 0 - Success, negative - Failure
 **/
static int api_cmd_create_cell(struct hinic_api_cmd_chain *chain,
			       int cell_idx,
			       struct hinic_api_cmd_cell *pre_node,
			       struct hinic_api_cmd_cell **node_vaddr)
{
	struct hinic_api_cmd_cell_ctxt *cell_ctxt;
	struct hinic_hwif *hwif = chain->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_api_cmd_cell *node;
	dma_addr_t node_paddr;
	int err;

	node = dma_zalloc_coherent(&pdev->dev, chain->cell_size,
				   &node_paddr, GFP_KERNEL);
	if (!node) {
		dev_err(&pdev->dev, "Failed to allocate dma API CMD cell\n");
		return -ENOMEM;
	}

	node->read.hw_wb_resp_paddr = 0;

	cell_ctxt = &chain->cell_ctxt[cell_idx];
	cell_ctxt->cell_vaddr = node;
	cell_ctxt->cell_paddr = node_paddr;

	if (!pre_node) {
		chain->head_cell_paddr = node_paddr;
		chain->head_node = node;
	} else {
		/* The data in the HW should be in Big Endian Format */
		pre_node->next_cell_paddr = cpu_to_be64(node_paddr);
	}

	switch (chain->chain_type) {
	case HINIC_API_CMD_WRITE_TO_MGMT_CPU:
		err = alloc_cmd_buf(chain, node, cell_idx);
		if (err) {
			dev_err(&pdev->dev, "Failed to allocate cmd buffer\n");
			goto err_alloc_cmd_buf;
		}
		break;

	default:
		dev_err(&pdev->dev, "Unsupported API CMD chain type\n");
		err = -EINVAL;
		goto err_alloc_cmd_buf;
	}

	*node_vaddr = node;
	return 0;

err_alloc_cmd_buf:
	dma_free_coherent(&pdev->dev, chain->cell_size, node, node_paddr);
	return err;
}

/**
 * api_cmd_destroy_cell - destroy API CMD cell of specific chain
 * @chain: the API CMD specific chain to destroy its cell
 * @cell_idx: the cell to destroy
 **/
static void api_cmd_destroy_cell(struct hinic_api_cmd_chain *chain,
				 int cell_idx)
{
	struct hinic_api_cmd_cell_ctxt *cell_ctxt;
	struct hinic_hwif *hwif = chain->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_api_cmd_cell *node;
	dma_addr_t node_paddr;
	size_t node_size;

	cell_ctxt = &chain->cell_ctxt[cell_idx];

	node = cell_ctxt->cell_vaddr;
	node_paddr = cell_ctxt->cell_paddr;
	node_size = chain->cell_size;

	if (cell_ctxt->api_cmd_vaddr) {
		switch (chain->chain_type) {
		case HINIC_API_CMD_WRITE_TO_MGMT_CPU:
			free_cmd_buf(chain, cell_idx);
			break;
		default:
			dev_err(&pdev->dev, "Unsupported API CMD chain type\n");
			break;
		}

		dma_free_coherent(&pdev->dev, node_size, node,
				  node_paddr);
	}
}

/**
 * api_cmd_destroy_cells - destroy API CMD cells of specific chain
 * @chain: the API CMD specific chain to destroy its cells
 * @num_cells: number of cells to destroy
 **/
static void api_cmd_destroy_cells(struct hinic_api_cmd_chain *chain,
				  int num_cells)
{
	int cell_idx;

	for (cell_idx = 0; cell_idx < num_cells; cell_idx++)
		api_cmd_destroy_cell(chain, cell_idx);
}

/**
 * api_cmd_create_cells - create API CMD cells for specific chain
 * @chain: the API CMD specific chain
 *
 * Return 0 - Success, negative - Failure
 **/
static int api_cmd_create_cells(struct hinic_api_cmd_chain *chain)
{
	struct hinic_api_cmd_cell *node = NULL, *pre_node = NULL;
	struct hinic_hwif *hwif = chain->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int err, cell_idx;

	for (cell_idx = 0; cell_idx < chain->num_cells; cell_idx++) {
		err = api_cmd_create_cell(chain, cell_idx, pre_node, &node);
		if (err) {
			dev_err(&pdev->dev, "Failed to create API CMD cell\n");
			goto err_create_cell;
		}

		pre_node = node;
	}

	/* set the Final node to point on the start */
	node->next_cell_paddr = cpu_to_be64(chain->head_cell_paddr);

	/* set the current node to be the head */
	chain->curr_node = chain->head_node;
	return 0;

err_create_cell:
	api_cmd_destroy_cells(chain, cell_idx);
	return err;
}

/**
 * api_chain_init - initialize API CMD specific chain
 * @chain: the API CMD specific chain to initialize
 * @attr: attributes to set in the chain
 *
 * Return 0 - Success, negative - Failure
 **/
static int api_chain_init(struct hinic_api_cmd_chain *chain,
			  struct hinic_api_cmd_chain_attr *attr)
{
	struct hinic_hwif *hwif = attr->hwif;
	struct pci_dev *pdev = hwif->pdev;
	size_t cell_ctxt_size;

	chain->hwif = hwif;
	chain->chain_type  = attr->chain_type;
	chain->num_cells = attr->num_cells;
	chain->cell_size = attr->cell_size;

	chain->prod_idx  = 0;
	chain->cons_idx  = 0;

	cell_ctxt_size = chain->num_cells * sizeof(*chain->cell_ctxt);
	chain->cell_ctxt = devm_kzalloc(&pdev->dev, cell_ctxt_size, GFP_KERNEL);
	if (!chain->cell_ctxt)
		return -ENOMEM;

	chain->wb_status = dma_zalloc_coherent(&pdev->dev,
					       sizeof(*chain->wb_status),
					       &chain->wb_status_paddr,
					       GFP_KERNEL);
	if (!chain->wb_status) {
		dev_err(&pdev->dev, "Failed to allocate DMA wb status\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * api_chain_free - free API CMD specific chain
 * @chain: the API CMD specific chain to free
 **/
static void api_chain_free(struct hinic_api_cmd_chain *chain)
{
	struct hinic_hwif *hwif = chain->hwif;
	struct pci_dev *pdev = hwif->pdev;

	dma_free_coherent(&pdev->dev, sizeof(*chain->wb_status),
			  chain->wb_status, chain->wb_status_paddr);
}

/**
 * api_cmd_create_chain - create API CMD specific chain
 * @attr: attributes to set the chain
 *
 * Return the created chain
 **/
static struct hinic_api_cmd_chain *
	api_cmd_create_chain(struct hinic_api_cmd_chain_attr *attr)
{
	struct hinic_hwif *hwif = attr->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_api_cmd_chain *chain;
	int err;

	if (attr->num_cells & (attr->num_cells - 1)) {
		dev_err(&pdev->dev, "Invalid number of cells, must be power of 2\n");
		return ERR_PTR(-EINVAL);
	}

	chain = devm_kzalloc(&pdev->dev, sizeof(*chain), GFP_KERNEL);
	if (!chain)
		return ERR_PTR(-ENOMEM);

	err = api_chain_init(chain, attr);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize chain\n");
		return ERR_PTR(err);
	}

	err = api_cmd_create_cells(chain);
	if (err) {
		dev_err(&pdev->dev, "Failed to create cells for API CMD chain\n");
		goto err_create_cells;
	}

	err = api_cmd_chain_hw_init(chain);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize chain HW\n");
		goto err_chain_hw_init;
	}

	return chain;

err_chain_hw_init:
	api_cmd_destroy_cells(chain, chain->num_cells);

err_create_cells:
	api_chain_free(chain);
	return ERR_PTR(err);
}

/**
 * api_cmd_destroy_chain - destroy API CMD specific chain
 * @chain: the API CMD specific chain to destroy
 **/
static void api_cmd_destroy_chain(struct hinic_api_cmd_chain *chain)
{
	api_cmd_destroy_cells(chain, chain->num_cells);
	api_chain_free(chain);
}

/**
 * hinic_api_cmd_init - Initialize all the API CMD chains
 * @chain: the API CMD chains that are initialized
 * @hwif: the hardware interface of a pci function device
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_api_cmd_init(struct hinic_api_cmd_chain **chain,
		       struct hinic_hwif *hwif)
{
	enum hinic_api_cmd_chain_type type, chain_type;
	struct hinic_api_cmd_chain_attr attr;
	struct pci_dev *pdev = hwif->pdev;
	size_t hw_cell_sz;
	int err;

	hw_cell_sz = sizeof(struct hinic_api_cmd_cell);

	attr.hwif = hwif;
	attr.num_cells  = API_CHAIN_NUM_CELLS;
	attr.cell_size  = API_CMD_CELL_SIZE(hw_cell_sz);

	chain_type = HINIC_API_CMD_WRITE_TO_MGMT_CPU;
	for ( ; chain_type < HINIC_API_CMD_MAX; chain_type++) {
		attr.chain_type = chain_type;

		if (chain_type != HINIC_API_CMD_WRITE_TO_MGMT_CPU)
			continue;

		chain[chain_type] = api_cmd_create_chain(&attr);
		if (IS_ERR(chain[chain_type])) {
			dev_err(&pdev->dev, "Failed to create chain %d\n",
				chain_type);
			goto err_create_chain;
		}
	}

	return 0;

err_create_chain:
	type = HINIC_API_CMD_WRITE_TO_MGMT_CPU;
	for ( ; type < chain_type; type++) {
		if (type != HINIC_API_CMD_WRITE_TO_MGMT_CPU)
			continue;

		api_cmd_destroy_chain(chain[type]);
	}

	return err;
}

/**
 * hinic_api_cmd_free - free the API CMD chains
 * @chain: the API CMD chains that are freed
 **/
void hinic_api_cmd_free(struct hinic_api_cmd_chain **chain)
{
	enum hinic_api_cmd_chain_type chain_type;

	chain_type = HINIC_API_CMD_WRITE_TO_MGMT_CPU;
	for ( ; chain_type < HINIC_API_CMD_MAX; chain_type++) {
		if (chain_type != HINIC_API_CMD_WRITE_TO_MGMT_CPU)
			continue;

		api_cmd_destroy_chain(chain[chain_type]);
	}
}
