// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2012-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "msm_qpic_nand.h"

#define QPIC_BAM_DEFAULT_IPC_LOGLVL 2

/* The driver supports devices up to 4K page */
#define MAX_CW_PER_PAGE 8
/*
 * Max descriptors needed for erase, read, write operations.
 * Usually, this is (2 * MAX_CW_PER_PAGE).
 */
#define MAX_DESC 16
#define SMEM_AARM_PARTITION_TABLE 9
#define SMEM_APPS 0
#define ONE_CODEWORD_SIZE 516
#define ACTIVE_BOOT_PART_MAX 30

static struct device *dev_node;
static char active_boot_part[ACTIVE_BOOT_PART_MAX] = "boot";

/* Variable to check boot device is NAND or not */
static bool is_bootdevice_nand = true;

/*
 * Function to get the active boot partition information
 * from kernel command line during system boot.
 */
#ifndef MODULE
static int __init get_active_boot_part(char *str)
{
	strscpy(active_boot_part, str, ACTIVE_BOOT_PART_MAX);
	return 0;
}

__setup("part.activeboot=", get_active_boot_part);
#endif

/* Function to check whether boot device is
 * NAND or not by reading boot_config register.
 */
static void msm_nand_boot_device_is_nand(struct platform_device *pdev)
{
	int i;
	u8 *buf;
	size_t len;
	/* Currently, NAND boot supports three different boot_conf values.
	 * In future, based on the number of supported values,
	 * the array size needs to be increased.
	 */
	u32 nand_boot[3], boot_dev_bits;
	struct nvmem_cell *cell;

	cell = nvmem_cell_get(&pdev->dev, "boot_conf");
	if (IS_ERR(cell)) {
		dev_err(&pdev->dev, "nvmem cell get failed err:(%ld)\n", PTR_ERR(cell));
		return;
	}

	buf = (u8 *)nvmem_cell_read(cell, &len);
	if (IS_ERR(buf)) {
		dev_err(&pdev->dev, "nvmem cell read failed err:(%ld)\n", PTR_ERR(buf));
		goto put_nvmem_cell;
	}

	if (of_property_read_u32(pdev->dev.of_node,
				   "qcom,boot_dev_bits",
				   &boot_dev_bits)) {
		dev_err(&pdev->dev, "number of bits to represent boot device not found\n");
		goto free_buf;
	}

	if (of_property_read_u32_array(pdev->dev.of_node,
					"qcom,nand_boot",
					nand_boot, ARRAY_SIZE(nand_boot))) {
		dev_err(&pdev->dev, "boot_config value for boot device not found\n");
		goto free_buf;
	}

	/* Storage boot device fuse is present in QFPROM_RAW_OEM_CONFIG_ROW0_LSB
	 * this fuse is blown by bootloader and populated in boot_config
	 * register[1:4] FAST_BOOT bits - hence shift read data by 1 and mask it with 0xf.
	 *
	 * FAST_BOOT bits might vary from target to target. It could be [1:3] or [1:4] or [1:5].
	 * So, get the FAST_BOOT bits information from dtsi and shift accordingly.
	 */
	for (i = 0; i < ARRAY_SIZE(nand_boot); i++) {
		is_bootdevice_nand = (((*buf >> 1) & ((1 << boot_dev_bits) - 1)) == nand_boot[i]) ?
										true : false;
		if (is_bootdevice_nand)
			break;
	}
	if (!is_bootdevice_nand)
		dev_err(&pdev->dev, "boot_config val = 0x%x\n",
				(*buf >> 1) & ((1 << boot_dev_bits) - 1));
free_buf:
	kfree(buf);
put_nvmem_cell:
	nvmem_cell_put(cell);
}

/*
 * Get the DMA memory for requested amount of size. It returns the pointer
 * to free memory available from the allocated pool. Returns NULL if there
 * is no free memory.
 */
static void *msm_nand_get_dma_buffer(struct msm_nand_chip *chip, size_t size)
{
	uint32_t bitmask, free_bitmask, old_bitmask;
	uint32_t need_mask, current_need_mask;
	int free_index;

	need_mask = (1UL << DIV_ROUND_UP(size, MSM_NAND_DMA_BUFFER_SLOT_SZ))
			- 1;
	bitmask = atomic_read(&chip->dma_buffer_busy);
	free_bitmask = ~bitmask;
	if (free_bitmask == 0)
		return NULL;

	do {
		free_index = __ffs(free_bitmask);
		current_need_mask = need_mask << free_index;

		if (size + free_index * MSM_NAND_DMA_BUFFER_SLOT_SZ >=
						 MSM_NAND_DMA_BUFFER_SIZE)
			return NULL;

		if ((bitmask & current_need_mask) == 0) {
			old_bitmask =
				atomic_cmpxchg(&chip->dma_buffer_busy,
					       bitmask,
					       bitmask | current_need_mask);
			if (old_bitmask == bitmask)
				return chip->dma_virt_addr +
				free_index * MSM_NAND_DMA_BUFFER_SLOT_SZ;
			free_bitmask = 0;/* force return */
		}
		/* current free range was too small, clear all free bits */
		/* below the top busy bit within current_need_mask */
		free_bitmask &=
			~(~0U >> (32 - fls(bitmask & current_need_mask)));
	} while (free_bitmask);

	return NULL;
}

/*
 * Releases the DMA memory used to the free pool and also wakes up any user
 * thread waiting on wait queue for free memory to be available.
 */
static void msm_nand_release_dma_buffer(struct msm_nand_chip *chip,
					void *buffer, size_t size)
{
	int index;
	uint32_t used_mask;

	used_mask = (1UL << DIV_ROUND_UP(size, MSM_NAND_DMA_BUFFER_SLOT_SZ))
			- 1;
	index = ((uint8_t *)buffer - chip->dma_virt_addr) /
		MSM_NAND_DMA_BUFFER_SLOT_SZ;
	atomic_sub(used_mask << index, &chip->dma_buffer_busy);

	wake_up(&chip->dma_wait_queue);
}

/*
 * Calculates page address of the buffer passed, offset of buffer within
 * that page and then maps it for DMA by calling dma_map_page().
 */
static dma_addr_t msm_nand_dma_map(struct device *dev, void *addr, size_t size,
					 enum dma_data_direction dir)
{
	struct page *page;
	unsigned long offset = (unsigned long)addr & ~PAGE_MASK;

	if (virt_addr_valid(addr))
		page = virt_to_page(addr);
	else {
		if (WARN_ON(size + offset > PAGE_SIZE))
			return ~0;
		page = vmalloc_to_page(addr);
	}
	return dma_map_page(dev, page, offset, size, dir);
}

static int msm_nand_bus_set_vote(struct msm_nand_info *info, bool vote)
{
	struct msm_nand_bus_vote_data *bvd = info->clk_data.bus_vote_data;
	struct msm_bus_path *usecase = bvd->usecase;
	struct msm_bus_vectors *vec = usecase[vote].vec;
	int ddr_rc;

	if (vote == bvd->curr_vote)
		return 0;

	pr_debug("vote:%d nand_ddr ab:%llu ib:%llu\n",
			vote, vec[0].ab, vec[0].ib);
	ddr_rc = icc_set_bw(bvd->nand_ddr, vec[0].ab, vec[0].ib);
	if (ddr_rc) {
		pr_err("icc_set() failed\n");
		goto out;
	}
	bvd->curr_vote = vote;
out:
	return ddr_rc;
}

static int msm_nand_setup_clocks_and_bus_bw(struct msm_nand_info *info,
				bool vote)
{
	int ret = 0;

	if (!info->clk_data.rpmh_clk) {
		if (IS_ERR_OR_NULL(info->clk_data.qpic_clk)) {
			ret = -EINVAL;
			goto out;
		}
	}
	if (atomic_read(&info->clk_data.clk_enabled) == vote)
		goto out;
	if (!atomic_read(&info->clk_data.clk_enabled) && vote) {
		ret = msm_nand_bus_set_vote(info, 1);
		if (ret) {
			pr_err("Failed to vote for bus with %d\n", ret);
			goto out;
		}
		if (!info->clk_data.rpmh_clk) {
			ret = clk_prepare_enable(info->clk_data.qpic_clk);
			if (ret) {
				pr_err("Failed to enable the bus-clock with error %d\n",
					ret);
				msm_nand_bus_set_vote(info, 0);
				goto out;
			}
		}
	} else if (atomic_read(&info->clk_data.clk_enabled) && !vote) {
		if (!info->clk_data.rpmh_clk)
			clk_disable_unprepare(info->clk_data.qpic_clk);
		msm_nand_bus_set_vote(info, 0);
	}
	atomic_set(&info->clk_data.clk_enabled, vote);
out:
	return ret;
}

#ifdef CONFIG_PM
static int msm_nand_runtime_suspend(struct device *dev)
{
	int ret = 0;
	struct msm_nand_info *info = dev_get_drvdata(dev);

	ret = msm_nand_setup_clocks_and_bus_bw(info, false);

	return ret;
}

static int msm_nand_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct msm_nand_info *info = dev_get_drvdata(dev);

	ret = msm_nand_setup_clocks_and_bus_bw(info, true);

	return ret;
}

static void msm_nand_print_rpm_info(struct device *dev)
{
	pr_err("RPM: runtime_status=%d, usage_count=%d, is_suspended=%d, disable_depth=%d, runtime_error=%d, request_pending=%d, request=%d\n",
		dev->power.runtime_status, atomic_read(&dev->power.usage_count),
		dev->power.is_suspended, dev->power.disable_depth,
		dev->power.runtime_error, dev->power.request_pending,
		dev->power.request);
}
#else
static int msm_nand_runtime_suspend(struct device *dev)
{
	return 0;
}

static int msm_nand_runtime_resume(struct device *dev)
{
	return 0;
}

static void msm_nand_print_rpm_info(struct device *dev)
{
}
#endif

#ifdef CONFIG_PM
static int msm_nand_suspend(struct device *dev)
{
	int ret = 0;
	struct msm_nand_info *info;
	struct msm_nand_chip *chip;

	/* If boot device is not NAND return success */
	if (!is_bootdevice_nand)
		return 0;
	info = dev_get_drvdata(dev);
	chip = &info->nand_chip;
	mutex_lock(&info->lock);

	/* Returns true for Deep sleep/Quick boot case else false */
	if (pm_suspend_via_firmware()) {
		/* sps_deregister_bam_device  is accessing bam registers so enable clocks*/
		ret = msm_nand_get_device(chip->dev);
		if (ret)
			goto out;
		msm_nand_bam_free(info);
		ret = sps_deregister_bam_device(info->sps.bam_handle);
		if (ret) {
			pr_err("%s: sps_deregister_bam_device() failed with %d\n",
				__func__, ret);
			msm_nand_put_device(chip->dev);
			goto out;
		}
		ret = msm_nand_put_device(chip->dev);
		if (ret)
			goto out;
	}

	if (!pm_runtime_suspended(dev))
		ret = msm_nand_runtime_suspend(dev);

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int msm_nand_resume(struct device *dev)
{
	int ret = 0;
	struct msm_nand_info *info;

	/* If boot device is not NAND return success */
	if (!is_bootdevice_nand)
		return 0;
	info = dev_get_drvdata(dev);
	mutex_lock(&info->lock);

	if (!pm_runtime_suspended(dev))
		ret = msm_nand_runtime_resume(dev);

	mutex_unlock(&info->lock);

	/* Returns true for Deep sleep/Quick boot case else false */
	if (pm_suspend_via_firmware()) {
		ret = msm_nand_bam_init(info);
		if (ret) {
			pr_err("msm_nand_bam_init() failed %d\n", ret);
			goto out;
		}

		ret = msm_nand_enable_dma(info);
		if (ret) {
			pr_err("Failed to enable DMA in NANDc\n");
			goto out;
		}

		ret = msm_nand_boost_mode_enable(info);
		if (unlikely(ret))
			pr_err("Failed to enable Boost Mode Err: %d\n", ret);

		if (info->nand_chip.qpic_version >= 2 &&
			info->nand_chip.qpic_min_version >= 1) {
			ret = msm_nand_init_status_pipe(info);
		}
	}

out:
	return ret;
}
#else
static int msm_nand_suspend(struct device *dev)
{
	return 0;
}

static int msm_nand_resume(struct device *dev)
{
	return 0;
}
#endif

#ifdef CONFIG_PM
static int msm_nand_get_device(struct device *dev)
{
	int ret = 0;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pr_err("Failed to resume with %d\n", ret);
		msm_nand_print_rpm_info(dev);
	} else { /* Reset to success */
		ret = 0;
	}
	return ret;
}

static int msm_nand_put_device(struct device *dev)
{
	int ret = 0;

	pm_runtime_mark_last_busy(dev);
	ret = pm_runtime_put_autosuspend(dev);
	if (ret < 0) {
		pr_err("Failed to suspend with %d\n", ret);
		msm_nand_print_rpm_info(dev);
	} else { /* Reset to success */
		ret = 0;
	}
	return ret;
}
#else
static int msm_nand_get_device(struct device *dev)
{
	return 0;
}

static int msm_nand_put_device(struct device *dev)
{
	return 0;
}
#endif

static struct msm_nand_bus_vote_data *msm_nand_get_bus_vote_data(struct device
				       *dev)

{
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *of_node = dev->of_node;
	struct msm_nand_bus_vote_data *bvd = NULL;
	struct msm_bus_path *usecase = NULL;
	int ret = 0, i = 0, j, num_paths, len;
	const u32 *vec_arr = NULL;

	if (!pdev) {
		dev_err(dev, "Null platform device!\n");
		return NULL;
	}

	bvd = devm_kzalloc(dev, sizeof(*bvd), GFP_KERNEL);
	if (!bvd)
		return bvd;

	ret = of_property_read_string(of_node, "qcom,msm-bus,name",
					&bvd->name);
	if (ret) {
		dev_err(dev, "Bus name missing err:(%d)\n", ret);
		goto out;
	}

	ret = of_property_read_u32(of_node, "qcom,msm-bus,num-cases",
		&bvd->num_usecase);
	if (ret) {
		dev_err(dev, "num-usecases not found err:(%d)\n", ret);
		goto out;
	}

	usecase = devm_kzalloc(dev, (sizeof(struct msm_bus_path) *
				   bvd->num_usecase), GFP_KERNEL);
	if (!usecase)
		goto out;

	ret = of_property_read_u32(of_node, "qcom,msm-bus,num-paths",
				   &num_paths);
	if (ret) {
		dev_err(dev, "num_paths not found err:(%d)\n", ret);
		goto out;
	}

	vec_arr = of_get_property(of_node, "qcom,msm-bus,vectors-KBps", &len);
	if (!vec_arr) {
		dev_err(dev, "Vector array not found\n");
		goto out;
	}

	for (i = 0; i < bvd->num_usecase; i++) {
		usecase[i].num_paths = num_paths;
		usecase[i].vec = devm_kcalloc(dev, num_paths,
					      sizeof(struct msm_bus_vectors),
					      GFP_KERNEL);
		if (!usecase[i].vec)
			goto out;
		for (j = 0; j < num_paths; j++) {
			int idx = ((i * num_paths) + j) * 2;

			usecase[i].vec[j].ab = (u64)
				be32_to_cpu(vec_arr[idx]);
			usecase[i].vec[j].ib = (u64)
				be32_to_cpu(vec_arr[idx + 1]);
		}
	}

	bvd->usecase = usecase;
	return bvd;
out:
	bvd = NULL;
	return bvd;
}

static int msm_nand_bus_register(struct platform_device *pdev,
		struct msm_nand_info *info)
{
	struct msm_nand_bus_vote_data *bsd;
	struct device *dev = &pdev->dev;
	int ret = 0;

	bsd = msm_nand_get_bus_vote_data(dev);
	if (!bsd) {
		dev_err(&pdev->dev, "Failed to get bus_scale data\n");
		return -EINVAL;
	}
	info->clk_data.bus_vote_data = bsd;

	bsd->nand_ddr = of_icc_get(&pdev->dev, "nand-ddr");
	if (IS_ERR_OR_NULL(bsd->nand_ddr)) {
		dev_err(&pdev->dev, "(%ld): failed getting %s path\n",
			PTR_ERR(bsd->nand_ddr), "nand-ddr");
		ret = PTR_ERR(bsd->nand_ddr);
		bsd->nand_ddr = NULL;
		return ret;
	}

	return ret;
}

static void msm_nand_bus_unregister(struct msm_nand_info *info)
{
	struct msm_nand_bus_vote_data *bsd = info->clk_data.bus_vote_data;

	if (bsd)
		icc_put(bsd->nand_ddr);
}

/*
 * Wrapper function to prepare a single SPS command element with the data
 * that is passed to this function.
 */
static inline void msm_nand_prep_ce(struct sps_command_element *ce,
				uint32_t addr, uint32_t command, uint32_t data)
{
	ce->addr = addr;
	ce->command = (command & WRITE) ? (uint32_t) SPS_WRITE_COMMAND :
			(uint32_t) SPS_READ_COMMAND;
	ce->data = data;
	ce->mask = 0xFFFFFFFF;
}

/* To check interrupt mode is enable or disable */
static __always_inline bool msm_nand_is_interrupt_mode_enable(struct msm_nand_info *info)
{
	return (info->nand_chip.caps & MSM_NAND_INTERRUPT_MODE_ENABLE);
}

/* Waiting for descriptors completion on Polling/Interrupt mode. */
static inline int msm_nand_sps_poll_iovec(struct msm_nand_info *info,
						struct msm_nand_sps_endpt *end_point,
						unsigned int cnt, struct sps_iovec *iovec)
{
	int ret = 0;

	if (msm_nand_is_interrupt_mode_enable(info)) {
		/*
		 * Wait for IO completion, clear it for respective pipe to handle further
		 * interrupts on the pipe.
		 */
		wait_for_completion_io(&end_point->completion);
		switch (end_point->index) {
		case SPS_DATA_CONS_PIPE_INDEX:
			reinit_completion(&(info->sps.data_cons.completion));
			break;
		case SPS_DATA_PROD_PIPE_INDEX:
			reinit_completion(&(info->sps.data_prod.completion));
			break;
		case SPS_CMD_CONS_PIPE_INDEX:
			reinit_completion(&(info->sps.cmd_pipe.completion));
			break;
		case SPS_DATA_PROD_STAT_PIPE_INDEX:
			reinit_completion(&(info->sps.data_prod_stat.completion));
		}
	}

	do {
		do {
			ret = sps_get_iovec((end_point->handle), (iovec));
		} while (((iovec)->addr == 0x0) && ((iovec)->size == 0x0));
		if (ret) {
			dev_err(dev_node, "Failed to get iovec for pipe %d (err:%d)\n",
						(end_point->index), ret);
			return ret;
		}
	} while (--(cnt));

	return ret;
}

/* Wrapper function for descriptors completions.
 * @info - pointer to NANDc private data
 * @cmd_cnt - number of descriptors submitted on consumer cmd pipe
 * @pcnt - number of descriptors submitted on producer(read) data pipe
 * @pscnt - number of descriptors submitted on producer data status pipe
 * @ccnt - number of descriptors submitted on consumer(write) data pipe
 * @iovec - pointer to iovec.
 */
static inline int msm_nand_sps_get_iovec(struct msm_nand_info *info,
				unsigned int cmd_cnt, unsigned int pcnt,
				unsigned int pscnt, unsigned int ccnt,
						struct sps_iovec *iovec)
{
	int ret = 0;

	/* Poll on system consumer command pipe for descriptors completion */
	ret = msm_nand_sps_poll_iovec(info, &info->sps.cmd_pipe, cmd_cnt, iovec);
	if (ret)
		return ret;

	/* Poll on system producer data pipe for descriptors completion */
	if (pcnt) {
		ret = msm_nand_sps_poll_iovec(info, &info->sps.data_prod, pcnt, iovec);
		if (ret)
			return ret;
	}

	/*
	 * Poll on system producer data status pipe for descriptors completion
	 * the number of desc. is same as data desc.
	 */
	if (pscnt) {
		ret = msm_nand_sps_poll_iovec(info, &info->sps.data_prod_stat, pscnt, iovec);
		if (ret)
			return ret;
	}

	/* Poll on system consumer data pipe for descriptors completion */
	if (ccnt) {
		ret = msm_nand_sps_poll_iovec(info, &info->sps.data_cons, ccnt, iovec);
		if (ret)
			return ret;
	}

	return ret;
}

/*
 * Wrapper function to prepare a single command descriptor with a single
 * SPS command element with the data that is passed to this function.
 *
 * Since for any command element it is a must to have this flag
 * SPS_IOVEC_FLAG_CMD, this function by default updates this flag for a
 * command element that is passed and thus, the caller need not explicilty
 * pass this flag. The other flags must be passed based on the need.  If a
 * command element doesn't have any other flag, then 0 can be passed to flags.
 */
static inline void msm_nand_prep_single_desc(struct msm_nand_sps_cmd *sps_cmd,
				uint32_t addr, uint32_t command,
				uint32_t data, uint32_t flags)
{
	msm_nand_prep_ce(&sps_cmd->ce, addr, command, data);
	sps_cmd->flags = SPS_IOVEC_FLAG_CMD | flags;
}

/*
 * Read or Write a single NANDc register as mentioned by its parameter addr.
 * The return value indicates whether read or write is successful or not.
 */
static int msm_nand_flash_rd_rw_reg(struct msm_nand_info *info, uint32_t addr,
				uint32_t *val, uint32_t command)
{
	int ret = 0, submitted_num_desc = 1;
	struct msm_nand_sps_cmd *cmd;
	struct msm_nand_chip *chip = &info->nand_chip;
	struct {
		struct msm_nand_sps_cmd cmd;
		uint32_t data;
	} *dma_buffer;
	struct sps_iovec iovec_temp;

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer(
		    chip, sizeof(*dma_buffer))));
	cmd = &dma_buffer->cmd;
	msm_nand_prep_single_desc(cmd, addr, command,
			(command == READ) ? msm_virt_to_dma(chip, &dma_buffer->data) : *val,
			SPS_IOVEC_FLAG_INT);

	mutex_lock(&info->lock);
	ret = msm_nand_get_device(chip->dev);
	if (ret)
		goto out;
	ret = sps_transfer_one(info->sps.cmd_pipe.handle,
			msm_virt_to_dma(chip, &cmd->ce),
			sizeof(struct sps_command_element), NULL, cmd->flags);
	if (ret) {
		pr_err("failed to submit command %x ret %d\n", addr, ret);
		msm_nand_put_device(chip->dev);
		goto out;
	}

	ret = msm_nand_sps_get_iovec(info, submitted_num_desc, 0, 0, 0, &iovec_temp);
	if (ret)
		goto out;

	ret = msm_nand_put_device(chip->dev);
	if (ret)
		goto out;

	if (command == READ)
		*val = dma_buffer->data;
out:
	mutex_unlock(&info->lock);
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
	return ret;
}

/*
 * Read the Flash ID from the Nand Flash Device. The return value < 0
 * indicates failure. When successful, the Flash ID is stored in parameter
 * read_id.
 */
#define READID_CMDS 5
static int msm_nand_flash_read_id(struct msm_nand_info *info,
		bool read_onfi_signature, uint32_t *read_id,
		uint32_t *read_id2)
{
	int err = 0, i = 0;
	struct msm_nand_sps_cmd *cmd;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;
	struct msm_nand_chip *chip = &info->nand_chip;
	/*
	 * The following 5 commands are required to read id -
	 * write commands - addr0, flash, exec
	 * read_commands - read_id, read_id2
	 */
	struct {
		struct sps_transfer xfer;
		struct sps_iovec cmd_iovec[READID_CMDS];
		struct msm_nand_sps_cmd cmd[READID_CMDS];
		uint32_t data[READID_CMDS];
	} *dma_buffer;

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer
				(chip, sizeof(*dma_buffer))));
	if (read_onfi_signature)
		dma_buffer->data[0] = FLASH_READ_ONFI_SIGNATURE_ADDRESS;
	else
		dma_buffer->data[0] = FLASH_READ_DEVICE_ID_ADDRESS;

	dma_buffer->data[1] = EXTENDED_FETCH_ID | MSM_NAND_CMD_FETCH_ID;
	dma_buffer->data[2] = 1;
	dma_buffer->data[3] = 0xeeeeeeee;
	dma_buffer->data[4] = 0xeeeeeeee;

	cmd = dma_buffer->cmd;
	msm_nand_prep_single_desc(cmd, MSM_NAND_ADDR0(info), WRITE,
			dma_buffer->data[0], SPS_IOVEC_FLAG_LOCK);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_FLASH_CMD(info), WRITE,
			dma_buffer->data[1], 0);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_EXEC_CMD(info), WRITE,
			dma_buffer->data[2], SPS_IOVEC_FLAG_NWD);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_READ_ID(info), READ,
			msm_virt_to_dma(chip, &dma_buffer->data[3]), 0);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_READ_ID2(info), READ,
			msm_virt_to_dma(chip, &dma_buffer->data[4]),
			SPS_IOVEC_FLAG_UNLOCK | SPS_IOVEC_FLAG_INT);
	cmd++;

	WARN_ON(cmd - dma_buffer->cmd > READID_CMDS);
	dma_buffer->xfer.iovec_count = (cmd - dma_buffer->cmd);
	dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
	dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
					&dma_buffer->cmd_iovec);
	iovec = dma_buffer->xfer.iovec;

	for (i = 0; i < dma_buffer->xfer.iovec_count; i++) {
		iovec->addr =  msm_virt_to_dma(chip, &dma_buffer->cmd[i].ce);
		iovec->size = sizeof(struct sps_command_element);
		iovec->flags = dma_buffer->cmd[i].flags;
		iovec++;
	}

	mutex_lock(&info->lock);
	err = msm_nand_get_device(chip->dev);
	if (err)
		goto out;
	err =  sps_transfer(info->sps.cmd_pipe.handle, &dma_buffer->xfer);
	if (err) {
		pr_err("Failed to submit commands %d\n", err);
		msm_nand_put_device(chip->dev);
		goto out;
	}
	err = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count, 0, 0, 0, &iovec_temp);
	if (err)
		goto out;
	pr_debug("Read ID register value 0x%x\n", dma_buffer->data[3]);
	if (!read_onfi_signature)
		pr_debug("nandid: %x maker %02x device %02x\n",
		       dma_buffer->data[3], dma_buffer->data[3] & 0xff,
		       (dma_buffer->data[3] >> 8) & 0xff);
	*read_id = dma_buffer->data[3];
	if (read_id2) {
		pr_debug("Extended Read ID register value 0x%x\n",
				dma_buffer->data[4]);
		*read_id2 = dma_buffer->data[4];
	}
	err = msm_nand_put_device(chip->dev);
out:
	mutex_unlock(&info->lock);
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
	return err;
}

/*
 * Contains data for common configuration registers that must be programmed
 * for every NANDc operation.
 */
struct msm_nand_common_cfgs {
	uint32_t cmd;
	uint32_t addr0;
	uint32_t addr1;
	uint32_t cfg0;
	uint32_t cfg1;
};

/*
 * Function to prepare SPS command elements to write into NANDc configuration
 * registers as per the data defined in struct msm_nand_common_cfgs. This is
 * required for the following NANDc operations - Erase, Bad Block checking
 * and for reading ONFI parameter page.
 */
static void msm_nand_prep_cfg_cmd_desc(struct msm_nand_info *info,
				struct msm_nand_common_cfgs data,
				struct msm_nand_sps_cmd **curr_cmd)
{
	struct msm_nand_sps_cmd *cmd;

	cmd = *curr_cmd;
	msm_nand_prep_single_desc(cmd, MSM_NAND_FLASH_CMD(info), WRITE,
			data.cmd, SPS_IOVEC_FLAG_LOCK);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_ADDR0(info), WRITE,
			data.addr0, 0);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_ADDR1(info), WRITE,
			data.addr1, 0);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_DEV0_CFG0(info), WRITE,
			data.cfg0, 0);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_DEV0_CFG1(info), WRITE,
			data.cfg1, 0);
	cmd++;
	*curr_cmd = cmd;
}

/*
 * Function to check the CRC integrity check on ONFI parameter page read.
 * For ONFI parameter page read, the controller ECC will be disabled. Hence,
 * it is mandatory to manually compute CRC and check it against the value
 * stored within ONFI page.
 */
static uint16_t msm_nand_flash_onfi_crc_check(uint8_t *buffer, uint16_t count)
{
	int i;
	uint16_t result;

	for (i = 0; i < count; i++)
		buffer[i] = bitrev8(buffer[i]);

	result = bitrev16(crc16(bitrev16(0x4f4e), buffer, count));

	for (i = 0; i < count; i++)
		buffer[i] = bitrev8(buffer[i]);

	return result;
}

/*
 * Structure that contains NANDc register data for commands required
 * for reading ONFI parameter page.
 */
struct msm_nand_flash_onfi_data {
	struct msm_nand_common_cfgs cfg;
	uint32_t exec;
	uint32_t ecc_bch_cfg;
};

struct version {
	uint16_t nand_major;
	uint16_t nand_minor;
	uint16_t qpic_major;
	uint16_t qpic_minor;
};

static int msm_nand_version_check(struct msm_nand_info *info,
			struct version *nandc_version)
{
	uint32_t qpic_ver = 0, nand_ver = 0;
	int err = 0;

	/* Lookup the version to identify supported features */
	err = msm_nand_flash_rd_rw_reg(info, MSM_NAND_VERSION(info),
		&nand_ver, READ);
	if (err) {
		pr_err("Failed to read NAND_VERSION, err=%d\n", err);
		goto out;
	}
	nandc_version->nand_major = (nand_ver & MSM_NAND_VERSION_MAJOR_MASK) >>
		MSM_NAND_VERSION_MAJOR_SHIFT;
	nandc_version->nand_minor = (nand_ver & MSM_NAND_VERSION_MINOR_MASK) >>
		MSM_NAND_VERSION_MINOR_SHIFT;

	err = msm_nand_flash_rd_rw_reg(info, MSM_NAND_QPIC_VERSION(info),
		&qpic_ver, READ);
	if (err) {
		pr_err("Failed to read QPIC_VERSION, err=%d\n", err);
		goto out;
	}
	nandc_version->qpic_major = (qpic_ver & MSM_NAND_VERSION_MAJOR_MASK) >>
			MSM_NAND_VERSION_MAJOR_SHIFT;
	nandc_version->qpic_minor = (qpic_ver & MSM_NAND_VERSION_MINOR_MASK) >>
			MSM_NAND_VERSION_MINOR_SHIFT;
	pr_info("nand_major:%d, nand_minor:%d, qpic_major:%d, qpic_minor:%d\n",
		nandc_version->nand_major, nandc_version->nand_minor,
		nandc_version->qpic_major, nandc_version->qpic_minor);
out:
	return err;
}

/*
 * Function to identify whether the attached NAND flash device is
 * complaint to ONFI spec or not. If yes, then it reads the ONFI parameter
 * page to get the device parameters.
 */
#define ONFI_CMDS 10
static int msm_nand_flash_onfi_probe(struct msm_nand_info *info)
{
	struct msm_nand_chip *chip = &info->nand_chip;
	struct flash_identification *flash = &info->flash_dev;
	uint32_t crc_chk_count = 0, page_address = 0;
	int ret = 0, i = 0, submitted_num_desc = 1;
	uint32_t manid, devid;

	/* SPS parameters */
	struct msm_nand_sps_cmd *cmd, *curr_cmd;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;
	uint32_t rdata;

	/* ONFI Identifier/Parameter Page parameters */
	uint8_t *onfi_param_info_buf = NULL;
	dma_addr_t dma_addr_param_info = 0;
	struct onfi_param_page *onfi_param_page_ptr;
	struct msm_nand_flash_onfi_data data;
	uint32_t onfi_signature = 0;

	/*
	 * The following 9 commands are required to get onfi parameters -
	 * flash, addr0, addr1, cfg0, cfg1, dev0_ecc_cfg,
	 * read_loc_0, exec, flash_status (read cmd).
	 */
	struct {
		struct sps_transfer xfer;
		struct sps_iovec cmd_iovec[ONFI_CMDS];
		struct msm_nand_sps_cmd cmd[ONFI_CMDS];
		uint32_t flash_status;
	} *dma_buffer;


	/* Lookup the version to identify supported features */
	struct version nandc_version = {0};

	ret = msm_nand_version_check(info, &nandc_version);
	if (!ret && !((nandc_version.nand_major == 1 &&
			nandc_version.nand_minor >= 5 &&
			nandc_version.qpic_major == 1 &&
			nandc_version.qpic_minor >= 5) ||
			(nandc_version.nand_major >= 2 &&
			nandc_version.qpic_major >= 2))) {
		ret = -EPERM;
		goto out;
	}
	wait_event(chip->dma_wait_queue, (onfi_param_info_buf =
		msm_nand_get_dma_buffer(chip, ONFI_PARAM_INFO_LENGTH)));
	dma_addr_param_info = msm_virt_to_dma(chip, onfi_param_info_buf);

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer
				(chip, sizeof(*dma_buffer))));

	ret = msm_nand_flash_read_id(info, 1, &onfi_signature, NULL);
	if (ret < 0) {
		pr_err("Failed to read ONFI signature\n");
		goto free_dma;
	}
	if (onfi_signature != ONFI_PARAMETER_PAGE_SIGNATURE) {
		ret = -EIO;
		goto free_dma;
	}

	memset(&data, 0, sizeof(struct msm_nand_flash_onfi_data));

	/* Lookup the partition to which apps has access to
	 *
	 * active_boot_part value gets updated to either kernel command line
	 * parameter "part.activeboot=" value (if present) or hold the default
	 * "boot" value.
	 */
	for (i = 0; i < FLASH_PTABLE_MAX_PARTS_V4; i++) {
		if (mtd_part[i].name && !strcmp(active_boot_part, mtd_part[i].name)) {
			page_address = mtd_part[i].offset << 6;
			break;
		}
	}

	if (!page_address) {
		pr_err("%s: no apps partition found in smem\n", __func__);
		ret = -EPERM;
		goto free_dma;
	}

	data.cfg.cmd = MSM_NAND_CMD_PAGE_READ_ONFI;
	data.exec = 1;
	data.cfg.addr0 = (page_address << 16) |
				FLASH_READ_ONFI_PARAMETERS_ADDRESS;
	data.cfg.addr1 = (page_address >> 16) & 0xFF;
	data.cfg.cfg0 =	MSM_NAND_CFG0_RAW_ONFI_PARAM_INFO;
	data.cfg.cfg1 = MSM_NAND_CFG1_RAW_ONFI_PARAM_INFO;
	data.ecc_bch_cfg = 1 << ECC_CFG_ECC_DISABLE;
	dma_buffer->flash_status = 0xeeeeeeee;

	curr_cmd = cmd = dma_buffer->cmd;
	msm_nand_prep_cfg_cmd_desc(info, data.cfg, &curr_cmd);

	cmd = curr_cmd;
	msm_nand_prep_single_desc(cmd, MSM_NAND_DEV0_ECC_CFG(info), WRITE,
			data.ecc_bch_cfg, 0);
	cmd++;

	rdata = (0 << 0) | (ONFI_PARAM_INFO_LENGTH << 16) | (1 << 31);
	msm_nand_prep_single_desc(cmd, MSM_NAND_READ_LOCATION_0(info), WRITE,
			rdata, 0);
	cmd++;

	if (chip->qpic_version >= 2) {
		msm_nand_prep_single_desc(cmd,
			MSM_NAND_READ_LOCATION_LAST_CW_0(info), WRITE,
			rdata, 0);
		cmd++;
	}

	msm_nand_prep_single_desc(cmd, MSM_NAND_EXEC_CMD(info), WRITE,
		data.exec, SPS_IOVEC_FLAG_NWD);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_FLASH_STATUS(info), READ,
		msm_virt_to_dma(chip, &dma_buffer->flash_status),
		SPS_IOVEC_FLAG_UNLOCK | SPS_IOVEC_FLAG_INT);
	cmd++;

	WARN_ON(cmd - dma_buffer->cmd > ONFI_CMDS);
	dma_buffer->xfer.iovec_count = (cmd - dma_buffer->cmd);
	dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
	dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
					&dma_buffer->cmd_iovec);
	iovec = dma_buffer->xfer.iovec;

	for (i = 0; i < dma_buffer->xfer.iovec_count; i++) {
		iovec->addr =  msm_virt_to_dma(chip,
				&dma_buffer->cmd[i].ce);
		iovec->size = sizeof(struct sps_command_element);
		iovec->flags = dma_buffer->cmd[i].flags;
		iovec++;
	}
	mutex_lock(&info->lock);
	ret = msm_nand_get_device(chip->dev);
	if (ret)
		goto unlock_mutex;
	/* Submit data descriptor */
	ret = sps_transfer_one(info->sps.data_prod.handle, dma_addr_param_info,
			ONFI_PARAM_INFO_LENGTH, NULL, SPS_IOVEC_FLAG_INT);
	if (ret) {
		pr_err("Failed to submit data descriptors %d\n", ret);
		goto put_dev;
	}
	/* Submit command descriptors */
	ret =  sps_transfer(info->sps.cmd_pipe.handle,
			&dma_buffer->xfer);
	if (ret) {
		pr_err("Failed to submit commands %d\n", ret);
		goto put_dev;
	}
	ret = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count,
					submitted_num_desc, 0, 0, &iovec_temp);
	if (ret)
		goto put_dev;

	ret = msm_nand_put_device(chip->dev);
	mutex_unlock(&info->lock);
	if (ret)
		goto free_dma;

	/* Check for flash status errors */
	if (dma_buffer->flash_status & (FS_OP_ERR | FS_MPU_ERR)) {
		pr_err("MPU/OP err (0x%x) is set\n", dma_buffer->flash_status);
		ret = -EIO;
		goto free_dma;
	}

	for (crc_chk_count = 0; crc_chk_count < ONFI_PARAM_INFO_LENGTH
			/ ONFI_PARAM_PAGE_LENGTH; crc_chk_count++) {
		onfi_param_page_ptr =
			(struct onfi_param_page *)
			(&(onfi_param_info_buf
			[ONFI_PARAM_PAGE_LENGTH *
			crc_chk_count]));
		if (msm_nand_flash_onfi_crc_check(
			(uint8_t *)onfi_param_page_ptr,
			ONFI_PARAM_PAGE_LENGTH - 2) ==
			onfi_param_page_ptr->integrity_crc) {
			break;
		}
	}
	if (crc_chk_count >= ONFI_PARAM_INFO_LENGTH
			/ ONFI_PARAM_PAGE_LENGTH) {
		pr_err("CRC Check failed on param page\n");
		ret = -EIO;
		goto free_dma;
	}
	ret = msm_nand_flash_read_id(info, 0, &flash->flash_id, NULL);
	if (ret < 0) {
		pr_err("Failed to read flash ID\n");
		goto free_dma;
	}
	flash->widebus  = onfi_param_page_ptr->features_supported & 0x01;
	flash->pagesize = onfi_param_page_ptr->number_of_data_bytes_per_page;
	flash->blksize  = onfi_param_page_ptr->number_of_pages_per_block *
					flash->pagesize;
	flash->oobsize  = onfi_param_page_ptr->number_of_spare_bytes_per_page;
	flash->density  = onfi_param_page_ptr->number_of_logical_units *
		onfi_param_page_ptr->number_of_blocks_per_logical_unit *
					flash->blksize;
	flash->ecc_correctability =
			onfi_param_page_ptr->number_of_bits_ecc_correctability;
	flash->timing_mode_support = onfi_param_page_ptr->timing_mode_support;

	pr_info("Found an ONFI compliant device %s\n",
			onfi_param_page_ptr->device_model);

	manid  = flash->flash_id & 0xFF;
	devid  = (flash->flash_id >> 8) & 0xFF;
	flash->is_onfi_compliant = true;

	/* hack for 8 x 8 JSC MCP part */
	if (manid == 0xAD && devid == 0xA3)
		flash->density = flash->density * 2;

	/*
	 * Temporary hack for MT29F4G08ABC device.
	 * Since the device is not properly adhering
	 * to ONFi specification it is reporting
	 * as 16 bit device though it is 8 bit device!!!
	 */
	if (!strcmp(onfi_param_page_ptr->device_model, "MT29F4G08ABC"))
		flash->widebus  = 0;
	goto free_dma;
put_dev:
	msm_nand_put_device(chip->dev);
unlock_mutex:
	mutex_unlock(&info->lock);
free_dma:
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
	msm_nand_release_dma_buffer(chip, onfi_param_info_buf,
			ONFI_PARAM_INFO_LENGTH);
out:
	return ret;
}

/*
 * Structure that contains read/write parameters required for reading/writing
 * from/to a page.
 */
struct msm_nand_rw_params {
	uint32_t page;
	uint32_t page_count;
	uint32_t sectordatasize;
	uint32_t sectoroobsize;
	uint32_t cwperpage;
	uint32_t oob_len_cmd;
	uint32_t oob_len_data;
	uint32_t start_sector;
	uint32_t oob_col;
	uint32_t mp_to_read; /* number of multi pages to read */
	dma_addr_t data_dma_addr;
	dma_addr_t oob_dma_addr;
	dma_addr_t ecc_dma_addr;
	dma_addr_t data_dma_addr_curr;
	dma_addr_t oob_dma_addr_curr;
	dma_addr_t ecc_dma_addr_curr;
	bool read;
	bool is_mp_read; /* set if it is a multi page read */
};

/*
 * Structure that contains NANDc register data required for reading/writing
 * from/to a page.
 */
struct msm_nand_rw_reg_data {
	uint32_t cmd;
	uint32_t addr0;
	uint32_t addr1;
	uint32_t cfg0;
	uint32_t cfg1;
	uint32_t ecc_bch_cfg;
	uint32_t exec;
	uint32_t ecc_cfg;
	uint32_t clrfstatus;
	uint32_t clrrstatus;
};

/*
 * Function that validates page read/write MTD parameters received from upper
 * layers such as MTD/YAFFS2 and returns error for any unsupported operations
 * by the driver. In case of success, it also maps the data and oob buffer
 * received for DMA.
 */
static int msm_nand_validate_mtd_params(struct mtd_info *mtd, bool read,
					loff_t offset,
					struct mtd_oob_ops *ops,
					struct msm_nand_rw_params *args)
{
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	int err = 0;

	pr_debug("========================================================\n");
	pr_debug("offset 0x%llx mode %d\ndatbuf 0x%pK datlen 0x%zx\n",
			offset, ops->mode, ops->datbuf, ops->len);
	pr_debug("oobbuf 0x%pK ooblen 0x%zx\n", ops->oobbuf, ops->ooblen);

	if (ops->mode == MTD_OPS_PLACE_OOB) {
		pr_err("MTD_OPS_PLACE_OOB is not supported\n");
		err = -EINVAL;
		goto out;
	}

	if (mtd->writesize == PAGE_SIZE_2K)
		args->page = offset >> 11;

	if (mtd->writesize == PAGE_SIZE_4K)
		args->page = offset >> 12;

	args->oob_len_cmd = ops->ooblen;
	args->oob_len_data = ops->ooblen;
	args->cwperpage = (mtd->writesize >> 9);
	args->read = (read ? true : false);

	if (offset & (mtd->writesize - 1)) {
		pr_err("unsupported offset 0x%llx\n", offset);
		err = -EINVAL;
		goto out;
	}

	if (!read && !ops->datbuf) {
		pr_err("No data buffer provided for write!!\n");
		err = -EINVAL;
		goto out;
	}

	if (ops->mode == MTD_OPS_RAW) {
		if (!ops->datbuf) {
			pr_err("No data buffer provided for RAW mode\n");
			err =  -EINVAL;
			goto out;
		} else if ((ops->len % (mtd->writesize +
				mtd->oobsize)) != 0) {
			pr_err("unsupported data len %zu for RAW mode\n",
				ops->len);
			err = -EINVAL;
			goto out;
		}
		if (ops->len <= ONE_CODEWORD_SIZE)
			args->page_count = 1;
		else
			args->page_count = ops->len /
				(mtd->writesize + mtd->oobsize);

	} else if (ops->mode == MTD_OPS_AUTO_OOB) {
		if (ops->datbuf && (ops->len %
			((ops->len <= ONE_CODEWORD_SIZE) ?
			ONE_CODEWORD_SIZE : mtd->writesize)) != 0) {
			/* when ops->datbuf is NULL, ops->len can be ooblen */
			pr_err("unsupported data len %zu for AUTO mode\n",
					ops->len);
			err = -EINVAL;
			goto out;
		}
		if (read && ops->oobbuf && !ops->datbuf) {
			args->start_sector = args->cwperpage - 1;
			args->page_count = ops->ooblen / mtd->oobavail;
			if ((args->page_count == 0) && (ops->ooblen))
				args->page_count = 1;
		} else if (ops->datbuf) {
			if (ops->len <= ONE_CODEWORD_SIZE)
				args->page_count = 1;
			else
				args->page_count = ops->len / mtd->writesize;
		}
	}

	if (ops->datbuf) {
		if (read)
			memset(ops->datbuf, 0xFF, ops->len);
		args->data_dma_addr_curr = args->data_dma_addr =
			msm_nand_dma_map(chip->dev, ops->datbuf, ops->len,
				      (read ? DMA_FROM_DEVICE : DMA_TO_DEVICE));
		if (dma_mapping_error(chip->dev, args->data_dma_addr)) {
			pr_err("dma mapping failed for 0x%pK\n", ops->datbuf);
			err = -EIO;
			goto out;
		}
	}
	if (ops->oobbuf) {
		if (read)
			memset(ops->oobbuf, 0xFF, ops->ooblen);
		args->oob_dma_addr_curr = args->oob_dma_addr =
			msm_nand_dma_map(chip->dev, ops->oobbuf, ops->ooblen,
				(read ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE));
		if (dma_mapping_error(chip->dev, args->oob_dma_addr)) {
			pr_err("dma mapping failed for 0x%pK\n", ops->oobbuf);
			err = -EIO;
			goto dma_map_oobbuf_failed;
		}
	}
	goto out;
dma_map_oobbuf_failed:
	if (ops->datbuf)
		dma_unmap_page(chip->dev, args->data_dma_addr, ops->len,
				(read ? DMA_FROM_DEVICE : DMA_TO_DEVICE));
out:
	return err;
}

/*
 * Function that updates NANDc register data (struct msm_nand_rw_reg_data)
 * required for page read/write.
 */
static void msm_nand_update_rw_reg_data(struct msm_nand_chip *chip,
					struct mtd_oob_ops *ops,
					struct msm_nand_rw_params *args,
					struct msm_nand_rw_reg_data *data)
{
	/*
	 * While reading one codeword, CW_PER_PAGE bits of  QPIC_NAND_DEV0_CFG0
	 * should be set to 0, which implies 1 codeword per page. 'n' below,
	 * is used to configure cfg0 for reading one full page or one single
	 * codeword.
	 */
	int n = (ops->len <= ONE_CODEWORD_SIZE) ? args->cwperpage : 1;

	if (args->read) {
		if (ops->mode != MTD_OPS_RAW) {
			data->cmd = MSM_NAND_CMD_PAGE_READ_ECC;
			data->cfg0 =
			(chip->cfg0 & ~(7U << CW_PER_PAGE)) |
			(((args->cwperpage-n) - args->start_sector)
			 << CW_PER_PAGE);
			data->cfg1 = chip->cfg1;
			data->ecc_bch_cfg = chip->ecc_bch_cfg;
		} else {
			data->cmd = MSM_NAND_CMD_PAGE_READ_ALL;
			data->cfg0 =
			(chip->cfg0_raw & ~(7U << CW_PER_PAGE)) |
			(((args->cwperpage-n) - args->start_sector)
			 << CW_PER_PAGE);
			data->cfg1 = chip->cfg1_raw;
			data->ecc_bch_cfg = chip->ecc_cfg_raw;
		}

	} else {
		if (ops->mode != MTD_OPS_RAW) {
			data->cmd = MSM_NAND_CMD_PRG_PAGE;
			data->cfg0 = chip->cfg0;
			data->cfg1 = chip->cfg1;
			data->ecc_bch_cfg = chip->ecc_bch_cfg;
		} else {
			data->cmd = MSM_NAND_CMD_PRG_PAGE_ALL;
			data->cfg0 = chip->cfg0_raw;
			data->cfg1 = chip->cfg1_raw;
			data->ecc_bch_cfg = chip->ecc_cfg_raw;
		}
		data->clrfstatus = MSM_NAND_RESET_FLASH_STS;
		data->clrrstatus = MSM_NAND_RESET_READ_STS;
	}
	data->exec = 1;
	data->ecc_cfg = chip->ecc_buf_cfg;
}

/*
 * Function to prepare series of SPS command descriptors required for a page
 * read/write operation.
 */
static void msm_nand_prep_rw_cmd_desc(struct mtd_oob_ops *ops,
				struct msm_nand_rw_params *args,
				struct msm_nand_rw_reg_data *data,
				struct msm_nand_info *info,
				uint32_t curr_cw,
				struct msm_nand_rw_cmd_desc *cmd_list,
				uint32_t *cw_desc_cnt,
				uint32_t ecc_parity_bytes)
{
	struct msm_nand_chip *chip = &info->nand_chip;
	uint32_t rdata;
	/* read_location register parameters */
	uint32_t offset, size, last_read;
	struct sps_command_element *curr_ce, *start_ce;
	uint32_t *flags_ptr, *num_ce_ptr;
	/*
	 * Variable to configure read_location register parameters
	 * while reading one codeword or one full page
	 */
	int n = (ops->len <= ONE_CODEWORD_SIZE) ? args->cwperpage : 1;

	if (curr_cw == args->start_sector) {
		curr_ce = start_ce = &cmd_list->setup_desc.ce[0];
		num_ce_ptr = &cmd_list->setup_desc.num_ce;
		flags_ptr = &cmd_list->setup_desc.flags;
		*flags_ptr = CMD_LCK;
		cmd_list->count = 1;
		msm_nand_prep_ce(curr_ce, MSM_NAND_FLASH_CMD(info), WRITE,
				data->cmd);
		curr_ce++;

		msm_nand_prep_ce(curr_ce, MSM_NAND_ADDR0(info), WRITE,
				data->addr0);
		curr_ce++;

		msm_nand_prep_ce(curr_ce, MSM_NAND_ADDR1(info), WRITE,
				data->addr1);
		curr_ce++;

		msm_nand_prep_ce(curr_ce, MSM_NAND_DEV0_CFG0(info), WRITE,
				data->cfg0);
		curr_ce++;

		msm_nand_prep_ce(curr_ce, MSM_NAND_DEV0_CFG1(info), WRITE,
				data->cfg1);
		curr_ce++;

		msm_nand_prep_ce(curr_ce, MSM_NAND_DEV0_ECC_CFG(info), WRITE,
				data->ecc_bch_cfg);
		curr_ce++;

		msm_nand_prep_ce(curr_ce, MSM_NAND_EBI2_ECC_BUF_CFG(info),
				WRITE, data->ecc_cfg);
		curr_ce++;

		if (!args->read) {
			msm_nand_prep_ce(curr_ce, MSM_NAND_FLASH_STATUS(info),
					WRITE, data->clrfstatus);
			curr_ce++;
			goto sub_exec_cmd;
		} else {
			msm_nand_prep_ce(curr_ce,
					MSM_NAND_ERASED_CW_DETECT_CFG(info),
					WRITE, CLR_ERASED_PAGE_DET);
			curr_ce++;
			msm_nand_prep_ce(curr_ce,
					MSM_NAND_ERASED_CW_DETECT_CFG(info),
					WRITE, SET_ERASED_PAGE_DET);
			curr_ce++;
		}
	} else {
		curr_ce = start_ce = &cmd_list->cw_desc[*cw_desc_cnt].ce[0];
		num_ce_ptr = &cmd_list->cw_desc[*cw_desc_cnt].num_ce;
		flags_ptr = &cmd_list->cw_desc[*cw_desc_cnt].flags;
		*cw_desc_cnt += 1;
		*flags_ptr = CMD;
		cmd_list->count++;
	}
	if (!args->read)
		goto sub_exec_cmd;

	if (ops->mode == MTD_OPS_RAW) {
		if (ecc_parity_bytes) {
			rdata = (BYTES_517 << 0) | (ecc_parity_bytes << 16)
				| (1 << 31);
			msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_0(info),
					WRITE, rdata);
			curr_ce++;
			if (chip->qpic_version >= 2) {
				msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_0(info),
					WRITE, rdata);
				curr_ce++;
			}
		} else {
			rdata = (0 << 0) | (chip->cw_size << 16) | (1 << 31);
			msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_0(info),
					WRITE, rdata);
			curr_ce++;
			if (chip->qpic_version >= 2) {
				msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_0(info),
					WRITE, rdata);
				curr_ce++;
			}
		}
	}
	if (ops->mode == MTD_OPS_AUTO_OOB) {
		if (ops->datbuf) {
			offset = 0;
			if (ops->len <= ONE_CODEWORD_SIZE) {
				size = ONE_CODEWORD_SIZE;
				last_read = 1;
			} else {
				size = (curr_cw < (args->cwperpage - 1)) ? 516 :
					(512 - ((args->cwperpage - 1) << 2));
				last_read = (curr_cw < (args->cwperpage - 1)) ?
					1 : (ops->oobbuf ? 0 : 1);
			}
			rdata = (offset << 0) | (size << 16) |
				(last_read << 31);

			msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_0(info),
					WRITE,
					rdata);
			curr_ce++;
			if (chip->qpic_version >= 2) {
				msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_0(info),
					WRITE, rdata);
				curr_ce++;
			}
		}
		if (curr_cw == (args->cwperpage - n) && ops->oobbuf) {
			offset = 512 - ((args->cwperpage - 1) << 2);
			size = (args->cwperpage) << 2;
			if (size > args->oob_len_cmd)
				size = args->oob_len_cmd;
			args->oob_len_cmd -= size;
			last_read = 1;
			rdata = (offset << 0) | (size << 16) |
				(last_read << 31);

			if (!ops->datbuf) {
				msm_nand_prep_ce(curr_ce,
						MSM_NAND_READ_LOCATION_0(info),
						WRITE, rdata);
				curr_ce++;
				if (chip->qpic_version >= 2) {
					msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_0(info),
					WRITE, rdata);
					curr_ce++;
				}
			} else {
				msm_nand_prep_ce(curr_ce,
						MSM_NAND_READ_LOCATION_1(info),
						WRITE, rdata);
				curr_ce++;
				if (chip->qpic_version >= 2) {
					msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_1(info),
					WRITE, rdata);
					curr_ce++;
				}
			}
		}
	}
sub_exec_cmd:
	*flags_ptr |= NWD;
	msm_nand_prep_ce(curr_ce, MSM_NAND_EXEC_CMD(info), WRITE, data->exec);
	curr_ce++;

	*num_ce_ptr = curr_ce - start_ce;
}

/*
 * Function to prepare and submit SPS data descriptors required for a page
 * read/write operation.
 */
static int msm_nand_submit_rw_data_desc(struct mtd_oob_ops *ops,
				struct msm_nand_rw_params *args,
				struct msm_nand_info *info,
				uint32_t curr_cw,
				uint32_t ecc_parity_bytes)
{
	struct msm_nand_chip *chip = &info->nand_chip;
	struct sps_pipe *data_pipe_handle;
	uint32_t sectordatasize, sectoroobsize;
	uint32_t sps_flags = 0;
	int err = 0;
	/*
	 * Variable to configure sectordatasize and sectoroobsize
	 * while reading one codeword or one full page.
	 */
	int n = (ops->len <= ONE_CODEWORD_SIZE) ? args->cwperpage : 1;

	if (args->read)
		data_pipe_handle = info->sps.data_prod.handle;
	else
		data_pipe_handle = info->sps.data_cons.handle;

	if (ops->mode == MTD_OPS_RAW) {
		if (ecc_parity_bytes && args->read) {
			if (curr_cw == (args->cwperpage - n))
				sps_flags |= SPS_IOVEC_FLAG_INT;

			/* read only ecc bytes */
			err = sps_transfer_one(data_pipe_handle,
					args->ecc_dma_addr_curr,
					ecc_parity_bytes, NULL,
					sps_flags);
			if (err)
				goto out;
			args->ecc_dma_addr_curr += ecc_parity_bytes;
		} else {
			sectordatasize = chip->cw_size;
			if (!args->read)
				sps_flags = SPS_IOVEC_FLAG_EOT;
			if (curr_cw == (args->cwperpage - n))
				sps_flags |= SPS_IOVEC_FLAG_INT;

			err = sps_transfer_one(data_pipe_handle,
					args->data_dma_addr_curr,
					sectordatasize, NULL,
					sps_flags);
			if (err)
				goto out;
			args->data_dma_addr_curr += sectordatasize;
		}
	} else if (ops->mode == MTD_OPS_AUTO_OOB) {
		if (ops->datbuf) {
			if (ops->len <= ONE_CODEWORD_SIZE)
				sectordatasize = ONE_CODEWORD_SIZE;
			else
				sectordatasize =
					(curr_cw < (args->cwperpage - 1))
					? 516 :
					(512 - ((args->cwperpage - 1) << 2));

			if (!args->read) {
				sps_flags = SPS_IOVEC_FLAG_EOT;
				if (curr_cw == (args->cwperpage - 1) &&
						ops->oobbuf)
					sps_flags = 0;
			}
			if ((curr_cw == (args->cwperpage - n)) && !ops->oobbuf)
				sps_flags |= SPS_IOVEC_FLAG_INT;

			err = sps_transfer_one(data_pipe_handle,
					args->data_dma_addr_curr,
					sectordatasize, NULL,
					sps_flags);
			if (err)
				goto out;
			args->data_dma_addr_curr += sectordatasize;
		}

		if (ops->oobbuf && (curr_cw == (args->cwperpage - n))) {
			sectoroobsize = args->cwperpage << 2;
			if (sectoroobsize > args->oob_len_data)
				sectoroobsize = args->oob_len_data;

			if (!args->read)
				sps_flags |= SPS_IOVEC_FLAG_EOT;
			sps_flags |= SPS_IOVEC_FLAG_INT;
			err = sps_transfer_one(data_pipe_handle,
					args->oob_dma_addr_curr,
					sectoroobsize, NULL,
					sps_flags);
			if (err)
				goto out;
			args->oob_dma_addr_curr += sectoroobsize;
			args->oob_len_data -= sectoroobsize;
		}
	}
out:
	return err;
}

/*
 * Function to prepare SPS data descriptors required for a page
 * read/write operation.
 */
static void msm_nand_prepare_rw_data_desc(struct mtd_oob_ops *ops,
				struct msm_nand_rw_params *args,
				struct msm_nand_info *info,
				struct sps_iovec *iovec,
				uint32_t *des_count,
				uint32_t ecc_parity_bytes,
				int num_pages)
{
	struct msm_nand_chip *chip = &info->nand_chip;
	uint32_t sectordatasize, sectoroobsize;
	uint32_t sps_flags = 0, curr_cw;
	/*
	 * Variable to configure sectordatasize and sectoroobsize
	 * while reading one codeword or one full page.
	 */
	int n = (ops->len <= ONE_CODEWORD_SIZE) ? args->cwperpage : 1;

	while (num_pages--) {
		for (curr_cw = args->start_sector; curr_cw < args->cwperpage; curr_cw++) {
			if (ops->mode == MTD_OPS_RAW) {
				if (ecc_parity_bytes && args->read) {
					if (curr_cw == (args->cwperpage - n))
						sps_flags |= SPS_IOVEC_FLAG_INT;
					/* read only ecc bytes */
					iovec->addr = args->ecc_dma_addr_curr;
					iovec->size = ecc_parity_bytes;
					iovec->flags = sps_flags;
					iovec++;
					(*des_count)++;
					args->ecc_dma_addr_curr += ecc_parity_bytes;
				} else {
					sectordatasize = chip->cw_size;
					if (!args->read)
						sps_flags = SPS_IOVEC_FLAG_EOT;
					if (curr_cw == (args->cwperpage - n))
						sps_flags |= SPS_IOVEC_FLAG_INT;
					iovec->addr = args->data_dma_addr_curr;
					iovec->size = sectordatasize;
					iovec->flags = sps_flags;
					iovec++;
					(*des_count)++;
					args->data_dma_addr_curr += sectordatasize;
				}
			} else if (ops->mode == MTD_OPS_AUTO_OOB) {
				if (ops->datbuf) {
					if (ops->len <= ONE_CODEWORD_SIZE)
						sectordatasize = ONE_CODEWORD_SIZE;
					else
						sectordatasize =
							(curr_cw < (args->cwperpage - 1))
							? 516 :
							(512 - ((args->cwperpage - 1) << 2));

					/* As of now using current function for prepare read
					 * descriptors only.
					 * In future same function can use for prepare write
					 * descriptors also by including change assigning
					 * SPS_IOVEC_FLAG_EOT for each code word.
					 */
					if ((curr_cw == (args->cwperpage - n)) && !ops->oobbuf)
						sps_flags |= SPS_IOVEC_FLAG_INT;
					iovec->addr = args->data_dma_addr_curr;
					iovec->size = sectordatasize;
					iovec->flags = sps_flags;
					iovec++;
					(*des_count)++;
					args->data_dma_addr_curr += sectordatasize;
				}
				if (ops->oobbuf && (curr_cw == (args->cwperpage - n))) {
					sectoroobsize = args->cwperpage << 2;
					if (sectoroobsize > args->oob_len_data)
						sectoroobsize = args->oob_len_data;

					if (!args->read)
						sps_flags |= SPS_IOVEC_FLAG_EOT;
					sps_flags |= SPS_IOVEC_FLAG_INT;

					iovec->addr = args->oob_dma_addr_curr;
					iovec->size = sectoroobsize;
					iovec->flags = sps_flags;
					iovec++;
					(*des_count)++;
					args->oob_dma_addr_curr += sectoroobsize;
					args->oob_len_data -= sectoroobsize;
				}
			}
		}
	}
}

/*
 * Function to prepare read status descriptors for page scope
 * and multipage read operations.
 */
static void msm_nand_prepare_read_status_desc(struct mtd_oob_ops *ops,
				struct msm_nand_rw_params *args,
				struct msm_nand_info *info,
				struct sps_iovec *iovec,
				uint32_t *des_count,
				struct msm_nand_read_status_desc *status_desc,
				int num_pages)
{
	struct msm_nand_chip *chip = &info->nand_chip;
	uint32_t sps_flags = 0, curr_cw;

	while (num_pages--) {
		for (curr_cw = args->start_sector; curr_cw < args->cwperpage; curr_cw++) {
			/*
			 * As per QPIC2.0 HPG, Data Producer Status Pipe is used
			 * only to submit status descriptors for read page operations.
			 */
			if (ops->mode == MTD_OPS_AUTO_OOB) {
				if (ops->datbuf) {
					if ((curr_cw == (args->cwperpage - 1)) && !ops->oobbuf)
						sps_flags |= SPS_IOVEC_FLAG_INT;
					iovec->addr = msm_virt_to_dma(chip, status_desc);
					iovec->size = sizeof(*status_desc);
					iovec->flags = sps_flags;
					iovec++;
					(*des_count)++;
					status_desc++;
				}
				if (ops->oobbuf && (curr_cw == (args->cwperpage - 1))) {
					sps_flags |= SPS_IOVEC_FLAG_INT;
					iovec->addr = msm_virt_to_dma(chip, status_desc);
					iovec->size = sizeof(*status_desc);
					iovec->flags = sps_flags;
					iovec++;
					(*des_count)++;
					status_desc++;
				}
			} else if (ops->mode == MTD_OPS_RAW) {
				if (args->read) {
					if (curr_cw == (args->cwperpage - 1))
						sps_flags |= SPS_IOVEC_FLAG_INT;
					iovec->addr = msm_virt_to_dma(chip, status_desc);
					iovec->size = sizeof(*status_desc);
					iovec->flags = sps_flags;
					iovec++;
					(*des_count)++;
					status_desc++;
				}
			}
		}
	}
}

/*
 *
 * Function to prepare series of SPS command descriptors required for a page
 * read operation with enhanced read pagescope feature.
 */
static void msm_nand_prep_read_cmd_desc_pagescope(struct mtd_oob_ops *ops,
				struct msm_nand_rw_params *args,
				struct msm_nand_rw_reg_data *data,
				struct msm_nand_info *info,
				struct msm_nand_rw_cmd_desc *cmd_list,
				uint32_t ecc_parity_bytes)
{
	struct msm_nand_chip *chip = &info->nand_chip;
	uint32_t rdata;
	/* read_location register parameters */
	uint32_t offset, size, last_read;
	struct sps_command_element *curr_ce, *start_ce;
	uint32_t *flags_ptr, *num_ce_ptr;
	uint32_t auto_status_value = 0x0;

	curr_ce = start_ce = &cmd_list->setup_desc.ce[0];
	num_ce_ptr = &cmd_list->setup_desc.num_ce;
	flags_ptr = &cmd_list->setup_desc.flags;
	*flags_ptr = CMD_LCK;
	cmd_list->count = 1;

	auto_status_value = (NAND_FLASH_STATUS_EN |
				NANDC_BUFFER_STATUS_EN |
				NAND_ERASED_CW_DETECT_STATUS_EN |
				NAND_FLASH_STATUS_LAST_CW_EN |
				NANDC_BUFFER_STATUS_LAST_CW_EN |
				NAND_ERASED_CW_DETECT_STATUS_LAST_CW_EN);

	msm_nand_prep_ce(curr_ce, MSM_NAND_DEV0_CFG0(info), WRITE,
			data->cfg0);
	curr_ce++;

	msm_nand_prep_ce(curr_ce, MSM_NAND_DEV0_CFG1(info), WRITE,
			data->cfg1);
	curr_ce++;

	msm_nand_prep_ce(curr_ce, MSM_NAND_DEV0_ECC_CFG(info), WRITE,
			data->ecc_bch_cfg);
	curr_ce++;

	msm_nand_prep_ce(curr_ce, MSM_NAND_AUTO_STATUS_EN(info), WRITE,
			auto_status_value);
	curr_ce++;

	/* This is a 0 based register. So, do -1 to the actual page reads */
	if (args->is_mp_read) {
		msm_nand_prep_ce(curr_ce, MSM_NAND_MULTI_PAGE_CMD(info), WRITE,
				(args->mp_to_read - 1));
		curr_ce++;
	}

	msm_nand_prep_ce(curr_ce, MSM_NAND_ADDR0(info), WRITE,
			data->addr0);
	curr_ce++;

	msm_nand_prep_ce(curr_ce, MSM_NAND_ADDR1(info), WRITE,
			data->addr1);
	curr_ce++;
	*num_ce_ptr = curr_ce - start_ce;

	/* Prepare next set of command descriptors */

	curr_ce = start_ce = &cmd_list->cw_desc[0].ce[0];
	num_ce_ptr = &cmd_list->cw_desc[0].num_ce;
	flags_ptr = &cmd_list->cw_desc[0].flags;
	*flags_ptr = CMD;
	cmd_list->count++;

	if (ops->mode == MTD_OPS_RAW) {
		if (ecc_parity_bytes) {
			rdata = (BYTES_517 << 0) | (ecc_parity_bytes << 16)
								| (1 << 31);
			msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_0(info),
					WRITE, rdata);
			curr_ce++;

			msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_0(info),
					WRITE, rdata);
			curr_ce++;

		} else {
			rdata = (0 << 0) | (chip->cw_size << 16) | (1 << 31);
			msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_0(info),
					WRITE, rdata);
			curr_ce++;
			msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_0(info),
					WRITE, rdata);
			curr_ce++;

		}
	}
	if (ops->mode == MTD_OPS_AUTO_OOB) {
		if (ops->datbuf) {
			offset = 0;
			size = BYTES_516;
			last_read = 1;
			rdata = (offset << 0) | (size << 16) |
						(last_read << 31);
			msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_0(info),
					WRITE, rdata);
			curr_ce++;
			size = (BYTES_512 - ((args->cwperpage - 1) << 2));
			last_read = (ops->oobbuf ? 0 : 1);
			rdata = (offset << 0) | (size << 16) |
						(last_read << 31);
			msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_0(info),
					WRITE, rdata);
			curr_ce++;
		}

		if (ops->oobbuf) {
			last_read = 1;
			offset = BYTES_512 - ((args->cwperpage - 1) << 2);
			size = (args->cwperpage) << 2;
			if (size > args->oob_len_cmd)
				size = args->oob_len_cmd;
			args->oob_len_cmd -= size;
			rdata = (offset << 0) | (size << 16) |
						(last_read << 31);

			if (!ops->datbuf) {
				msm_nand_prep_ce(curr_ce,
						MSM_NAND_READ_LOCATION_0(info),
						WRITE, rdata);
				curr_ce++;
				msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_0(info),
								WRITE, rdata);
				curr_ce++;
			} else {
				msm_nand_prep_ce(curr_ce,
					MSM_NAND_READ_LOCATION_LAST_CW_1(info),
								WRITE, rdata);
				curr_ce++;
			}
		}
	}

	msm_nand_prep_ce(curr_ce, MSM_NAND_FLASH_CMD(info), WRITE,
			data->cmd);
	curr_ce++;

	*flags_ptr |= NWD;
	msm_nand_prep_ce(curr_ce, MSM_NAND_EXEC_CMD(info), WRITE, data->exec);
	curr_ce++;
	*num_ce_ptr = curr_ce - start_ce;
}

/*
 * Function to submit read status descriptors to
 * Data Producer Status Pipe during enhanced read Pagescope feature.
 */
static int msm_nand_submit_read_status_desc(struct mtd_oob_ops *ops,
				struct msm_nand_rw_params *args,
				struct msm_nand_info *info,
				uint32_t curr_cw,
				struct msm_nand_read_status_desc *status_desc)
{

	struct msm_nand_chip *chip = &info->nand_chip;
	struct sps_pipe *data_pipe_handle = NULL;
	uint32_t sps_flags = 0;
	int err = 0;

	/*
	 * As per QPIC2.0 HPG, Data Producer Status Pipe is used
	 * only to submit status descriptors for read page operations.
	 */
	if (args->read)
		data_pipe_handle = info->sps.data_prod_stat.handle;

	if (ops->mode == MTD_OPS_RAW) {
		if (args->read) {
			if (curr_cw == (args->cwperpage - 1))
				sps_flags |= SPS_IOVEC_FLAG_INT;

			err = sps_transfer_one(data_pipe_handle,
				msm_virt_to_dma(chip, status_desc),
				sizeof(*status_desc), NULL, sps_flags);
			if (err)
				goto out;
		}
	} else if (ops->mode == MTD_OPS_AUTO_OOB) {
		if (ops->datbuf) {
			if ((curr_cw == (args->cwperpage - 1)) && !ops->oobbuf)
				sps_flags |= SPS_IOVEC_FLAG_INT;

			err = sps_transfer_one(data_pipe_handle,
				msm_virt_to_dma(chip, status_desc),
				sizeof(*status_desc), NULL, sps_flags);
			if (err)
				goto out;
		}
		if (ops->oobbuf && (curr_cw == (args->cwperpage - 1))) {
			status_desc++;
			sps_flags |= SPS_IOVEC_FLAG_INT;
			err = sps_transfer_one(data_pipe_handle,
					msm_virt_to_dma(chip, status_desc),
					sizeof(*status_desc), NULL, sps_flags);
			if (err)
				goto out;
		}
	}
out:
	if (err)
		pr_err("Failed to submit status descriptor for codeword=%d\n",
			curr_cw);
	return err;
}

/*
 * Refer msm_nand_is_erased_page() for comments.
 * It only differs from it in using pagescope read commands.
 */
static int msm_nand_is_erased_page_ps(struct mtd_info *mtd, loff_t from,
				struct mtd_oob_ops *ops,
				struct msm_nand_rw_params *rw_params,
				bool *erased_page)
{
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	uint32_t cwperpage = (mtd->writesize >> 9);
	int err, submitted_num_desc = 0;
	uint32_t n = 0, num_zero_bits = 0, total_ecc_byte_cnt;
	uint32_t cw_desc_cnt = 1;
	struct sps_command_element *curr_ce, *start_ce;
	struct msm_nand_rw_reg_data data;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;
	struct mtd_oob_ops raw_ops;

	/*
	 * The following commands will be sent only once, for every single
	 * page read operation using pagescope feature - addr0, addr1,
	 * dev0_cfg0, dev0_cfg1, dev0_ecc_cfg, auto_status, flash,
	 * read_location_0, read_location_1, read_location_last_cw_0,
	 * read_location_last_cw_1, exec.
	 */
	struct msm_nand_rw_cmd_desc *cmd_list = NULL;
	struct msm_nand_read_status_desc *status_desc = NULL;
	uint32_t flash_cmd = 0x0;
	struct {
		struct sps_transfer xfer;
		struct sps_iovec cmd_iovec[MAX_DESC];
		struct {
			uint32_t count;
			struct msm_nand_cmd_setup_desc setup_desc;
			struct msm_nand_cmd_cw_desc cw_desc[MAX_DESC];
		} cmd_list;
		struct {
			uint32_t flash_status;
			uint32_t buffer_status;
			uint32_t erased_cw_status;
			/* This extra +1 is for oobbuf case */
		} result[MAX_CW_PER_PAGE + 1];
	} *dma_buffer;
	uint8_t *ecc, *ecc_temp;

	total_ecc_byte_cnt = (chip->ecc_parity_bytes * cwperpage);
	memcpy(&raw_ops, ops, sizeof(struct mtd_oob_ops));
	raw_ops.mode = MTD_OPS_RAW;
	ecc = kzalloc(total_ecc_byte_cnt, GFP_KERNEL);
	if (!ecc)
		return -ENOMEM;

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer(
					chip, sizeof(*dma_buffer))));

	memset(&data, 0, sizeof(struct msm_nand_rw_reg_data));
	msm_nand_update_rw_reg_data(chip, &raw_ops, rw_params, &data);

	if (rw_params->read) {
		if (raw_ops.mode != MTD_OPS_RAW)
			data.cmd = MSM_NAND_CMD_PAGE_READ_ECC_PS;
		else
			data.cmd = MSM_NAND_CMD_PAGE_READ_ALL_PS;
	}

	cmd_list = (struct msm_nand_rw_cmd_desc *)&dma_buffer->cmd_list;
	status_desc =
		(struct msm_nand_read_status_desc *)&dma_buffer->result[0];

	/* map the ecc for dma operations */
	rw_params->ecc_dma_addr_curr = rw_params->ecc_dma_addr =
		dma_map_single(chip->dev, ecc, total_ecc_byte_cnt,
				DMA_FROM_DEVICE);

	data.addr0 = (rw_params->page << 16) | rw_params->oob_col;
	data.addr1 = (rw_params->page >> 16) & 0xff;
	for (n = rw_params->start_sector; n < cwperpage; n++) {
		dma_buffer->result[n].flash_status = 0xeeeeeeee;
		dma_buffer->result[n].buffer_status = 0xeeeeeeee;
		dma_buffer->result[n].erased_cw_status = 0xeeeeee00;
	}
	msm_nand_prep_read_cmd_desc_pagescope(&raw_ops, rw_params, &data,
			info, cmd_list, chip->ecc_parity_bytes);

	start_ce = &cmd_list->cw_desc[cw_desc_cnt].ce[0];
	curr_ce = start_ce;
	cmd_list->cw_desc[cw_desc_cnt].flags = CMD | INT_UNLCK;
	cmd_list->count++;
	msm_nand_prep_ce(curr_ce, MSM_NAND_AUTO_STATUS_EN(info),
			WRITE, flash_cmd);
	curr_ce++;
	cmd_list->cw_desc[cw_desc_cnt].num_ce = curr_ce - start_ce;

	dma_buffer->xfer.iovec_count = cmd_list->count;
	dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
	dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
			&dma_buffer->cmd_iovec);
	iovec = dma_buffer->xfer.iovec;

	iovec->addr =  msm_virt_to_dma(chip,
			&cmd_list->setup_desc.ce[0]);
	iovec->size = sizeof(struct sps_command_element) *
		cmd_list->setup_desc.num_ce;
	iovec->flags = cmd_list->setup_desc.flags;
	iovec++;
	for (n = 0; n < (cmd_list->count - 1); n++) {
		iovec->addr =  msm_virt_to_dma(chip,
				&cmd_list->cw_desc[n].ce[0]);
		iovec->size = sizeof(struct sps_command_element) *
			cmd_list->cw_desc[n].num_ce;
		iovec->flags = cmd_list->cw_desc[n].flags;
		iovec++;
	}
	mutex_lock(&info->lock);
	err = msm_nand_get_device(chip->dev);
	if (err)
		goto unlock_mutex;
	/* Submit data descriptors */
	for (n = rw_params->start_sector; n < cwperpage; n++) {
		err = msm_nand_submit_rw_data_desc(&raw_ops,
				rw_params, info, n,
				chip->ecc_parity_bytes);
		if (err) {
			pr_err("Failed to submit data descs %d\n", err);
			panic("error in nand driver\n");
			goto put_dev;
		}
	}
	submitted_num_desc = cwperpage - rw_params->start_sector;
	/* Submit Data Status Descriptors */
	for (n = rw_params->start_sector; n < cwperpage; n++) {
		err = msm_nand_submit_read_status_desc(&raw_ops,
					 rw_params, info,
					 n, status_desc);
		if (err) {
			pr_err("Failed to submit data status descs %d\n", err);
			panic("error in nand driver\n");
			goto put_dev;
		}
		status_desc++;
	}

	/* Submit command descriptors */
	err =  sps_transfer(info->sps.cmd_pipe.handle,
			&dma_buffer->xfer);
	if (err) {
		pr_err("Failed to submit commands %d\n", err);
		goto put_dev;
	}

	/* Poll for command, data and status descriptors completion */
	err = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count,
			submitted_num_desc, submitted_num_desc, 0, &iovec_temp);
	if (err)
		goto put_dev;

	err = msm_nand_put_device(chip->dev);
	mutex_unlock(&info->lock);
	if (err)
		goto free_dma;

	pr_debug("addr0: 0x%08x, addr1: 0x%08x\n", data.addr0, data.addr1);
	for (n = rw_params->start_sector; n < cwperpage; n++)
		pr_debug("cw %d: flash_sts %x buffr_sts %x, erased_cw_status: %x\n",
				n, dma_buffer->result[n].flash_status,
				dma_buffer->result[n].buffer_status,
				dma_buffer->result[n].erased_cw_status);

	goto free_dma;
put_dev:
	msm_nand_put_device(chip->dev);
unlock_mutex:
	mutex_unlock(&info->lock);
free_dma:
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
	/* unmap ecc dma memory */
	dma_unmap_single(chip->dev, rw_params->ecc_dma_addr,
			total_ecc_byte_cnt, DMA_FROM_DEVICE);
	/* check for bit flips in ecc data */
	ecc_temp = ecc;
	for (n = rw_params->start_sector; n < cwperpage; n++) {
		int last_pos = 0, next_pos = 0;
		int ecc_bytes_percw_in_bits = (chip->ecc_parity_bytes * 8);

		do {
			last_pos = find_next_zero_bit((void *)ecc_temp,
					ecc_bytes_percw_in_bits, next_pos);

			if (last_pos < ecc_bytes_percw_in_bits)
				num_zero_bits++;

			if (num_zero_bits > 4) {
				*erased_page = false;
				goto free_mem;
			}

			next_pos = last_pos + 1;
		} while (last_pos < ecc_bytes_percw_in_bits);

		num_zero_bits = last_pos = next_pos = 0;
		ecc_temp += chip->ecc_parity_bytes;
	}

	if ((n == cwperpage) && (num_zero_bits <= 4))
		*erased_page = true;
free_mem:
	kfree(ecc);
	return err;
}

/*
 * Function that gets called from upper layers such as MTD/YAFFS2 to read a
 * page with main or/and spare data.
 * Function to be called for enhanced read pagescope feature.
 */
static int msm_nand_read_pagescope(struct mtd_info *mtd, loff_t from,
				struct mtd_oob_ops *ops)
{
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	struct flash_identification *flash_dev = &info->flash_dev;
	uint32_t cwperpage = (mtd->writesize >> 9);
	int err = 0, pageerr = 0, rawerr = 0;
	uint32_t submitted_num_desc = 0;
	uint32_t n = 0, pages_read = 0, flash_cmd = 0x0;
	uint32_t ecc_errors = 0, total_ecc_errors = 0, ecc_capability;
	struct msm_nand_rw_params rw_params;
	struct msm_nand_rw_reg_data data;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;
	bool erased_page;
	uint64_t fix_data_in_pages = 0;

	/*
	 * The following commands will be sent only once, for every single
	 * page read operation using pagescope feature - addr0, addr1,
	 * dev0_cfg0, dev0_cfg1, dev0_ecc_cfg, auto_status, flash,
	 * read_location_0, read_location_1, read_location_last_cw_0,
	 * read_location_last_cw_1, exec.
	 */
	struct {
		struct sps_transfer xfer;
		struct sps_transfer xfer_data;
		struct sps_transfer xfer_status;
		struct sps_iovec cmd_iovec[MAX_DESC];
		struct sps_iovec data_iovec[MAX_CW_PER_PAGE + 1];
		struct sps_iovec status_iovec[MAX_CW_PER_PAGE + 1];
		struct {
			uint32_t count;
			struct msm_nand_cmd_setup_desc setup_desc;
			struct msm_nand_cmd_cw_desc cw_desc[MAX_DESC];
		} cmd_list;
		struct {
			uint32_t flash_status;
			uint32_t buffer_status;
			uint32_t erased_cw_status;
			/* This extra +1 is for oobbuf case */
		} result[MAX_CW_PER_PAGE + 1];
	} *dma_buffer;
	struct msm_nand_rw_cmd_desc *cmd_list = NULL;
	struct msm_nand_read_status_desc *status_desc = NULL;

	memset(&rw_params, 0, sizeof(struct msm_nand_rw_params));
	err = msm_nand_validate_mtd_params(mtd, true, from, ops, &rw_params);
	if (err)
		goto validate_mtd_params_failed;

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer(
			    chip, sizeof(*dma_buffer))));

	rw_params.oob_col = rw_params.start_sector * chip->cw_size;
	if (chip->cfg1 & (1 << WIDE_FLASH))
		rw_params.oob_col >>= 1;

	memset(&data, 0, sizeof(struct msm_nand_rw_reg_data));
	msm_nand_update_rw_reg_data(chip, ops, &rw_params, &data);

	if (rw_params.read) {
		if (ops->mode != MTD_OPS_RAW)
			data.cmd = MSM_NAND_CMD_PAGE_READ_ECC_PS;
		else
			data.cmd = MSM_NAND_CMD_PAGE_READ_ALL_PS;
	}

	cmd_list = (struct msm_nand_rw_cmd_desc *)&dma_buffer->cmd_list;
	ecc_capability = flash_dev->ecc_capability;

	while (rw_params.page_count-- > 0) {

		uint32_t cw_desc_cnt = 1;
		struct sps_command_element *curr_ce, *start_ce;

		status_desc =
			(struct msm_nand_read_status_desc *)&dma_buffer->result[0];
		erased_page = false;
		data.addr0 = (rw_params.page << 16) | rw_params.oob_col;
		data.addr1 = (rw_params.page >> 16) & 0xff;

		for (n = rw_params.start_sector; n < cwperpage; n++) {
			dma_buffer->result[n].flash_status = 0xeeeeeeee;
			dma_buffer->result[n].buffer_status = 0xeeeeeeee;
			dma_buffer->result[n].erased_cw_status = 0xeeeeee00;
		}
		msm_nand_prep_read_cmd_desc_pagescope(ops, &rw_params,
							&data, info,
						cmd_list, 0);
		start_ce = &cmd_list->cw_desc[cw_desc_cnt].ce[0];
		curr_ce = start_ce;
		cmd_list->cw_desc[cw_desc_cnt].flags = CMD | INT_UNLCK;
		cmd_list->count++;
		msm_nand_prep_ce(curr_ce, MSM_NAND_AUTO_STATUS_EN(info),
				WRITE, flash_cmd);
		curr_ce++;
		cmd_list->cw_desc[cw_desc_cnt].num_ce = curr_ce - start_ce;

		dma_buffer->xfer.iovec_count = cmd_list->count;
		dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
		dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
						&dma_buffer->cmd_iovec);
		iovec = dma_buffer->xfer.iovec;

		iovec->addr =  msm_virt_to_dma(chip,
				&cmd_list->setup_desc.ce[0]);
		iovec->size = sizeof(struct sps_command_element) *
			cmd_list->setup_desc.num_ce;
		iovec->flags = cmd_list->setup_desc.flags;
		iovec++;
		for (n = 0; n < (cmd_list->count - 1); n++) {
			iovec->addr =  msm_virt_to_dma(chip,
						&cmd_list->cw_desc[n].ce[0]);
			iovec->size = sizeof(struct sps_command_element) *
						cmd_list->cw_desc[n].num_ce;
			iovec->flags = cmd_list->cw_desc[n].flags;
			iovec++;
		}
		submitted_num_desc = 0;

		/* Prepare Data Descriptors
		 * this function is common for pagscope read and
		 * multipage read.
		 * pagescope read will prepare descriptors
		 * for one page, so last parameter set as one.
		 */
		msm_nand_prepare_rw_data_desc(ops, &rw_params, info,
				dma_buffer->data_iovec, &submitted_num_desc, 0, 1);
		dma_buffer->xfer_data.iovec = dma_buffer->data_iovec;
		dma_buffer->xfer_data.iovec_count = submitted_num_desc;
		dma_buffer->xfer_data.iovec_phys = msm_virt_to_dma(chip,
				&dma_buffer->data_iovec);
		submitted_num_desc = 0;

		/* Prepare Data Status Descriptors
		 * this function is common for pagscope read and
		 * multipage read.
		 * pagescope read will prepare descriptors
		 * for one page, so last parameter set as one.
		 */
		msm_nand_prepare_read_status_desc(ops, &rw_params, info, dma_buffer->status_iovec,
				&submitted_num_desc, status_desc, 1);
		dma_buffer->xfer_status.iovec = dma_buffer->status_iovec;
		dma_buffer->xfer_status.iovec_count = submitted_num_desc;
		dma_buffer->xfer_status.iovec_phys = msm_virt_to_dma(chip,
				&dma_buffer->status_iovec);

		mutex_lock(&info->lock);
		err = msm_nand_get_device(chip->dev);
		if (err)
			goto unlock_mutex;

		/* Submit Data Descriptors */
		err =  sps_transfer(info->sps.data_prod.handle,
				&dma_buffer->xfer_data);
		if (err) {
			pr_err("Failed to submit data descs %d\n", err);
			goto put_dev;
		}

		/* Submit Data Status Descriptors */
		err =  sps_transfer(info->sps.data_prod_stat.handle,
				&dma_buffer->xfer_status);
		if (err) {
			pr_err("Failed to submit data status descs %d\n", err);
			goto put_dev;
		}

		/* Submit command descriptors */
		err =  sps_transfer(info->sps.cmd_pipe.handle,
				&dma_buffer->xfer);
		if (err) {
			pr_err("Failed to submit commands %d\n", err);
			goto put_dev;
		}

		/* Poll for command, data and status descriptors completion */
		err = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count,
				dma_buffer->xfer_data.iovec_count,
				dma_buffer->xfer_status.iovec_count, 0, &iovec_temp);
		if (err)
			goto put_dev;

		err = msm_nand_put_device(chip->dev);
		mutex_unlock(&info->lock);
		if (err)
			goto free_dma;
		/* Check for flash status errors */
		pageerr = rawerr = 0;
		for (n = rw_params.start_sector; n < cwperpage; n++) {
			if (dma_buffer->result[n].flash_status & (FS_OP_ERR |
					FS_MPU_ERR)) {
				rawerr = -EIO;
				/*
				 * Check if ECC error was due to an erased
				 * codeword. If so, ignore the error.
				 *
				 * NOTE: There is a bug in erased page
				 * detection hardware block when reading
				 * only spare data. In order to work around
				 * this issue, instead of using PAGE_ALL_ERASED
				 * bit to check for whether a whole page is
				 * erased or not, we use CODEWORD_ALL_ERASED
				 * and  CODEWORD_ERASED bits together and check
				 * each codeword that has FP_OP_ERR bit set is
				 * an erased codeword or not.
				 */
				if ((dma_buffer->result[n].erased_cw_status &
					ERASED_CW) == ERASED_CW) {
					/*
					 * At least one code word is detected
					 * as an erased code word.
					 */
					pr_debug("erased codeword detected - ignore ecc error\n");
					continue;
				}
				pageerr = rawerr;
				break;
			}
		}
		/* check for uncorrectable errors */
		if (pageerr) {
			for (n = rw_params.start_sector; n < cwperpage; n++) {
				if (dma_buffer->result[n].buffer_status &
					BS_UNCORRECTABLE_BIT) {
					/*
					 * Check if page is actually
					 * erased or not.
					 */
					err = msm_nand_is_erased_page_ps(mtd,
							from, ops,
							&rw_params,
							&erased_page);
					if (err)
						goto free_dma;
					if (!erased_page) {
						mtd->ecc_stats.failed++;
						pageerr = -EBADMSG;
						break;
					}
					pageerr = 0;
					pr_debug("Uncorrectable ECC errors detected on an erased page and has been fixed.\n");
					break;
				}
			}
		}

		if (rawerr && !pageerr && erased_page) {
			/*
			 * This means an erased page had bit flips and now
			 * those bit-flips need to be cleared in the data
			 * being sent to upper layers. This will keep track
			 * of those pages and at the end, the data will be
			 * fixed before this function returns.
			 * Note that a whole page worth of data will be fixed
			 * and this will only handle about 64 pages being read
			 * at a time i.e. one erase block worth of pages.
			 */
			fix_data_in_pages |= BIT(rw_params.page_count);
		}
		/* check for correctable errors */
		if (!rawerr) {
			for (n = rw_params.start_sector; n < cwperpage; n++) {
				ecc_errors =
				    dma_buffer->result[n].buffer_status
				    & BS_CORRECTABLE_ERR_MSK;
				if (ecc_errors) {
					total_ecc_errors += ecc_errors;
					mtd->ecc_stats.corrected += ecc_errors;
				}
			}
		}
		if (pageerr && (pageerr != -EUCLEAN || err == 0))
			err = pageerr;

		if (rawerr && !pageerr) {
			pr_debug("%llx %zx %zx empty page\n",
			       (loff_t)rw_params.page * mtd->writesize,
			       ops->len, ops->ooblen);
		} else {
			for (n = rw_params.start_sector; n < cwperpage; n++)
				pr_debug("cw %d: flash_sts %x buffr_sts %x, erased_cw_status: %x, pageerr: %d, rawerr: %d\n",
				n, dma_buffer->result[n].flash_status,
				dma_buffer->result[n].buffer_status,
				dma_buffer->result[n].erased_cw_status,
				pageerr, rawerr);
		}
		if (err && err != -EUCLEAN && err != -EBADMSG)
			goto free_dma;
		pages_read++;
		rw_params.page++;
	}
	goto free_dma;
put_dev:
	msm_nand_put_device(chip->dev);
unlock_mutex:
	mutex_unlock(&info->lock);
free_dma:
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
	if (ops->oobbuf)
		dma_unmap_page(chip->dev, rw_params.oob_dma_addr,
				ops->ooblen, DMA_FROM_DEVICE);
	if (ops->datbuf)
		dma_unmap_page(chip->dev, rw_params.data_dma_addr,
				ops->len, DMA_BIDIRECTIONAL);
	/*
	 * If there were any erased pages detected with ECC errors, then
	 * it is most likely that the data is not all 0xff. So memset that
	 * page to all 0xff.
	 */
	while (fix_data_in_pages) {
		int temp_page = 0, oobsize = rw_params.cwperpage << 2;
		int count = 0, offset = 0;

		temp_page = fix_data_in_pages & BIT_MASK(0);
		fix_data_in_pages = fix_data_in_pages >> 1;
		count++;
		if (!temp_page)
			continue;
		offset = (count - 1) * mtd->writesize;
		if (ops->datbuf)
			memset((ops->datbuf + offset), 0xff, mtd->writesize);
		offset = (count - 1) * oobsize;
		if (ops->oobbuf)
			memset(ops->oobbuf + offset, 0xff, oobsize);
	}
validate_mtd_params_failed:
	if (ops->mode != MTD_OPS_RAW)
		ops->retlen = mtd->writesize * pages_read;
	else
		ops->retlen = (mtd->writesize +  mtd->oobsize) * pages_read;
	ops->oobretlen = ops->ooblen - rw_params.oob_len_data;
	if (err)
		pr_err("0x%llx datalen 0x%zx ooblen %zx err %d corrected %d\n",
			from, ops->datbuf ? ops->len : 0, ops->ooblen, err,
			total_ecc_errors);
	pr_debug("ret %d, retlen %zu oobretlen %zu\n",
			err, ops->retlen, ops->oobretlen);
	return err;
}

/*
 * Function that gets called from upper layers such as MTD/YAFFS2 to read a
 * page with main or/and spare data.
 * Function to be called for enhanced multipage feature.
 */
static int msm_nand_read_multipage(struct mtd_info *mtd, loff_t from,
				struct mtd_oob_ops *ops)
{
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	struct flash_identification *flash_dev = &info->flash_dev;
	uint32_t cwperpage = (mtd->writesize >> 9);
	int err = 0, pageerr = 0, rawerr = 0;
	uint32_t submitted_num_desc = 0;
	uint32_t n = 0, pages_read = 0, flash_cmd = 0x0;
	uint32_t ecc_errors = 0, total_ecc_errors = 0, ecc_capability;
	struct msm_nand_rw_params rw_params;
	struct msm_nand_rw_reg_data data;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;
	bool erased_page;
	uint64_t fix_data_in_pages = 0;
	int mp_to_read = 0;
	uint32_t cw_desc_cnt = 1;
	struct sps_command_element *curr_ce, *start_ce;

	/*
	 * The following commands will be sent only once, for every single
	 * page read operation using multipage read feature - addr0, addr1,
	 * dev0_cfg0, dev0_cfg1, dev0_ecc_cfg, auto_status, flash,
	 * read_location_0, read_location_1, read_location_last_cw_0,
	 * read_location_last_cw_1, exec.
	 */
	struct {
		struct sps_transfer xfer;
		struct sps_transfer xfer_data;
		struct sps_transfer xfer_status;
		struct sps_iovec cmd_iovec[MAX_DESC];
		struct sps_iovec data_iovec[(MAX_CW_PER_PAGE + 1) * MAX_MULTI_PAGE_READS];
		struct sps_iovec status_iovec[(MAX_CW_PER_PAGE + 1) * MAX_MULTI_PAGE_READS];
		struct {
			uint32_t count;
			struct msm_nand_cmd_setup_desc setup_desc;
			struct msm_nand_cmd_cw_desc cw_desc[MAX_DESC];
		} cmd_list;
		struct {
			uint32_t flash_status;
			uint32_t buffer_status;
			uint32_t erased_cw_status;
			/* This extra +1 is for oobbuf case */
		} result[(MAX_CW_PER_PAGE + 1) * MAX_MULTI_PAGE_READS];
	} *dma_buffer;
	struct msm_nand_rw_cmd_desc *cmd_list = NULL;
	struct msm_nand_read_status_desc *status_desc = NULL;

	memset(&rw_params, 0, sizeof(struct msm_nand_rw_params));
	err = msm_nand_validate_mtd_params(mtd, true, from, ops, &rw_params);
	if (err)
		goto validate_mtd_params_failed;

	if (rw_params.read)
		rw_params.is_mp_read = true;

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer(
			    chip, sizeof(*dma_buffer))));

	rw_params.oob_col = rw_params.start_sector * chip->cw_size;
	if (chip->cfg1 & (1 << WIDE_FLASH))
		rw_params.oob_col >>= 1;

	memset(&data, 0, sizeof(struct msm_nand_rw_reg_data));
	msm_nand_update_rw_reg_data(chip, ops, &rw_params, &data);

	if (rw_params.read) {
		if (ops->mode != MTD_OPS_RAW)
			data.cmd = MSM_NAND_CMD_PAGE_READ_ECC_MP;
		else
			data.cmd = MSM_NAND_CMD_PAGE_READ_ALL_MP;
	}

	cmd_list = (struct msm_nand_rw_cmd_desc *)&dma_buffer->cmd_list;
	ecc_capability = flash_dev->ecc_capability;

	while (rw_params.page_count > 0) {
		if (rw_params.page_count >= MAX_MULTI_PAGE_READS)
			mp_to_read = MAX_MULTI_PAGE_READS;
		else if (rw_params.page_count >= (MAX_MULTI_PAGE_READS/2))
			mp_to_read = (MAX_MULTI_PAGE_READS/2);
		else if (rw_params.page_count >= (MAX_MULTI_PAGE_READS/4))
			mp_to_read = (MAX_MULTI_PAGE_READS/4);
		else
			mp_to_read = (MAX_MULTI_PAGE_READS/8);

		rw_params.mp_to_read = mp_to_read;
		cw_desc_cnt = 1;

		status_desc =
			(struct msm_nand_read_status_desc *)&dma_buffer->result[0];
		erased_page = false;
		data.addr0 = (rw_params.page << 16) | rw_params.oob_col;
		data.addr1 = (rw_params.page >> 16) & 0xff;

		for (n = rw_params.start_sector; n < (cwperpage * mp_to_read); n++) {
			dma_buffer->result[n].flash_status = 0xeeeeeeee;
			dma_buffer->result[n].buffer_status = 0xeeeeeeee;
			dma_buffer->result[n].erased_cw_status = 0xeeeeee00;
		}
		msm_nand_prep_read_cmd_desc_pagescope(ops, &rw_params,
							&data, info,
						cmd_list, 0);
		start_ce = &cmd_list->cw_desc[cw_desc_cnt].ce[0];
		curr_ce = start_ce;
		cmd_list->cw_desc[cw_desc_cnt].flags = CMD | INT_UNLCK;
		cmd_list->count++;
		msm_nand_prep_ce(curr_ce, MSM_NAND_AUTO_STATUS_EN(info),
				WRITE, flash_cmd);
		curr_ce++;
		cmd_list->cw_desc[cw_desc_cnt].num_ce = curr_ce - start_ce;

		dma_buffer->xfer.iovec_count = cmd_list->count;
		dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
		dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
						&dma_buffer->cmd_iovec);
		iovec = dma_buffer->xfer.iovec;

		iovec->addr =  msm_virt_to_dma(chip,
				&cmd_list->setup_desc.ce[0]);
		iovec->size = sizeof(struct sps_command_element) *
			cmd_list->setup_desc.num_ce;
		iovec->flags = cmd_list->setup_desc.flags;
		iovec++;
		for (n = 0; n < (cmd_list->count - 1); n++) {
			iovec->addr =  msm_virt_to_dma(chip,
						&cmd_list->cw_desc[n].ce[0]);
			iovec->size = sizeof(struct sps_command_element) *
						cmd_list->cw_desc[n].num_ce;
			iovec->flags = cmd_list->cw_desc[n].flags;
			iovec++;
		}
		submitted_num_desc = 0;

		/* Prepare Data Descriptors */
		msm_nand_prepare_rw_data_desc(ops, &rw_params, info,
			dma_buffer->data_iovec, &submitted_num_desc, 0, mp_to_read);
		dma_buffer->xfer_data.iovec = dma_buffer->data_iovec;
		dma_buffer->xfer_data.iovec_count = submitted_num_desc;
		dma_buffer->xfer_data.iovec_phys = msm_virt_to_dma(chip,
				&dma_buffer->data_iovec);
		submitted_num_desc = 0;

		/* Prepare Data Status Descriptors */
		msm_nand_prepare_read_status_desc(ops, &rw_params, info,
				dma_buffer->status_iovec, &submitted_num_desc,
							status_desc, mp_to_read);
		dma_buffer->xfer_status.iovec = dma_buffer->status_iovec;
		dma_buffer->xfer_status.iovec_count = submitted_num_desc;
		dma_buffer->xfer_status.iovec_phys = msm_virt_to_dma(chip,
				&dma_buffer->status_iovec);

		mutex_lock(&info->lock);
		err = msm_nand_get_device(chip->dev);
		if (err)
			goto unlock_mutex;

		/* Submit Data Descriptors */
		err =  sps_transfer(info->sps.data_prod.handle,
				&dma_buffer->xfer_data);
		if (err) {
			pr_err("Failed to submit data descs %d\n", err);
			goto put_dev;
		}

		/* Submit Data Status Descriptors */
		err =  sps_transfer(info->sps.data_prod_stat.handle,
				&dma_buffer->xfer_status);
		if (err) {
			pr_err("Failed to submit data status descs %d\n", err);
			goto put_dev;
		}

		/* Submit command descriptors */
		err =  sps_transfer(info->sps.cmd_pipe.handle,
				&dma_buffer->xfer);
		if (err) {
			pr_err("Failed to submit commands %d\n", err);
			goto put_dev;
		}

		/* Poll for command, data and status descriptors completion */
		err = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count,
				dma_buffer->xfer_data.iovec_count,
				dma_buffer->xfer_status.iovec_count, 0, &iovec_temp);
		if (err)
			goto put_dev;

		err = msm_nand_put_device(chip->dev);
		mutex_unlock(&info->lock);
		if (err)
			goto free_dma;

		/* Check for flash status errors */
		pageerr = rawerr = 0;
		for (n = rw_params.start_sector; n < (cwperpage * mp_to_read); n++) {
			if (dma_buffer->result[n].flash_status & (FS_OP_ERR |
					FS_MPU_ERR)) {
				rawerr = -EIO;
				/*
				 * Check if ECC error was due to an erased
				 * codeword. If so, ignore the error.
				 *
				 * NOTE: There is a bug in erased page
				 * detection hardware block when reading
				 * only spare data. In order to work around
				 * this issue, instead of using PAGE_ALL_ERASED
				 * bit to check for whether a whole page is
				 * erased or not, we use CODEWORD_ALL_ERASED
				 * and  CODEWORD_ERASED bits together and check
				 * each codeword that has FP_OP_ERR bit set is
				 * an erased codeword or not.
				 */
				if ((dma_buffer->result[n].erased_cw_status &
					ERASED_CW) == ERASED_CW) {
					/*
					 * At least one code word is detected
					 * as an erased code word.
					 */
					pr_debug("erased codeword detected - ignore ecc error\n");
					continue;
				}
				pageerr = rawerr;
				break;
			}
		}
		/* check for uncorrectable errors */
		if (pageerr) {
			for (n = rw_params.start_sector; n < (cwperpage * mp_to_read); n++) {
				if (dma_buffer->result[n].buffer_status &
					BS_UNCORRECTABLE_BIT) {
					/*
					 * Check if page is actually
					 * erased or not.
					 */
					err = msm_nand_is_erased_page_ps(mtd,
							from, ops,
							&rw_params,
							&erased_page);
					if (err)
						goto free_dma;
					if (!erased_page) {
						mtd->ecc_stats.failed++;
						pageerr = -EBADMSG;
						break;
					}
					pageerr = 0;
					pr_debug("Uncorrectable ECC errors detected on an erased page and has been fixed.\n");
					break;
				}
			}
		}

		if (rawerr && !pageerr && erased_page) {
			/*
			 * This means an erased page had bit flips and now
			 * those bit-flips need to be cleared in the data
			 * being sent to upper layers. This will keep track
			 * of those pages and at the end, the data will be
			 * fixed before this function returns.
			 * Note that a whole page worth of data will be fixed
			 * and this will only handle about 64 pages being read
			 * at a time i.e. one erase block worth of pages.
			 */
			fix_data_in_pages |= BIT(rw_params.page_count);
		}
		/* check for correctable errors */
		if (!rawerr) {
			for (n = rw_params.start_sector; n < (cwperpage * mp_to_read); n++) {
				ecc_errors =
				    dma_buffer->result[n].buffer_status
				    & BS_CORRECTABLE_ERR_MSK;
				if (ecc_errors) {
					total_ecc_errors += ecc_errors;
					mtd->ecc_stats.corrected += ecc_errors;
				}
			}
		}
		if (pageerr && (pageerr != -EUCLEAN || err == 0))
			err = pageerr;

		if (rawerr && !pageerr) {
			pr_debug("%llx %zx %zx empty page\n",
			       (loff_t)rw_params.page * mtd->writesize,
			       ops->len, ops->ooblen);
		} else {
			for (n = rw_params.start_sector; n < (cwperpage * mp_to_read); n++)
				pr_debug("cw %d: flash_sts %x buffr_sts %x, erased_cw_status: %x, pageerr: %d, rawerr: %d\n",
				n, dma_buffer->result[n].flash_status,
				dma_buffer->result[n].buffer_status,
				dma_buffer->result[n].erased_cw_status,
				pageerr, rawerr);
		}
		if (err && err != -EUCLEAN && err != -EBADMSG)
			goto free_dma;
		pages_read += mp_to_read;
		rw_params.page += mp_to_read;
		rw_params.page_count -= mp_to_read;
	}
	goto free_dma;
put_dev:
	msm_nand_put_device(chip->dev);
unlock_mutex:
	mutex_unlock(&info->lock);
free_dma:
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
	if (ops->oobbuf)
		dma_unmap_page(chip->dev, rw_params.oob_dma_addr,
				ops->ooblen, DMA_FROM_DEVICE);
	if (ops->datbuf)
		dma_unmap_page(chip->dev, rw_params.data_dma_addr,
				ops->len, DMA_BIDIRECTIONAL);
	/*
	 * If there were any erased pages detected with ECC errors, then
	 * it is most likely that the data is not all 0xff. So memset that
	 * page to all 0xff.
	 */
	while (fix_data_in_pages) {
		int temp_page = 0, oobsize = rw_params.cwperpage << 2;
		int count = 0, offset = 0;

		temp_page = fix_data_in_pages & BIT_MASK(0);
		fix_data_in_pages = fix_data_in_pages >> 1;
		count++;
		if (!temp_page)
			continue;
		offset = (count - 1) * mtd->writesize;
		if (ops->datbuf)
			memset((ops->datbuf + offset), 0xff, mtd->writesize);
		offset = (count - 1) * oobsize;
		if (ops->oobbuf)
			memset(ops->oobbuf + offset, 0xff, oobsize);
	}
validate_mtd_params_failed:
	if (ops->mode != MTD_OPS_RAW)
		ops->retlen = mtd->writesize * pages_read;
	else
		ops->retlen = (mtd->writesize +  mtd->oobsize) * pages_read;
	ops->oobretlen = ops->ooblen - rw_params.oob_len_data;
	if (err)
		pr_err("0x%llx datalen 0x%zx ooblen %zx err %d corrected %d\n",
			from, ops->datbuf ? ops->len : 0, ops->ooblen, err,
			total_ecc_errors);
	pr_debug("ret %d, retlen %zu oobretlen %zu\n",
			err, ops->retlen, ops->oobretlen);
	return err;
}

/*
 * Read ECC bytes and check whether page is erased or not.
 *
 * The NAND devices manufactured with newer process node technology are
 * susceptible to bit-flips. These bit-flips are easily fixable with the
 * ECC engine and ECC information stored on the NAND device. This device
 * specific information is found in the data sheet for the NAND device
 * and is usually specified as a "number of bit-flips expected per code-
 * word". For example, "a single bit-flip per codeword". Also this means
 * that the number of ECC errors don't increase over period of time as in
 * the past and can't be used to predict a "bad-block about to happen"
 * situation anymore.
 *
 * So what this means to erased pages:
 * Since ECC data for an erased page is all 0xFF's, the ECC engine would
 * not be able to correct any bit-flips that occur in these newer parts.
 * If the NAND controller is unable to identify the erased page due to
 * the bit-flips, then there would be "uncorrectable ECC errors" detected
 * and would get reported to file system layer (YAFFS2/UBIFS etc) and would
 * result in a good block being marked as a bad block and also lead to
 * error scenarios.

 * So to handle this, the following will be done by software until newer
 * NAND controller hardware is avialable that can detected erased pages
 * with bit-flips successfully.
 *
 * 1. msm_nand_read_oob() calls this function when "uncorrectable ECC
 *	errors" occur.
 * 2. This function then performs a raw read of the page.
 * 3. This read is done to extract ECC bytes and not data from that page.
 * 4. For each codeword’s ECC data, the following is done
 *	a. Count number of zero bits
 *	b. If that count is greater than <BIT-FLIPS-EXPECTED>, then it is
 *		not an erased page.
 *	c. Else repeat for next codeword’s ECC data
 *	d. If all codewords have less than <BIT-FLIPS-EXPECTED> bits of
 *		zeros, then it’s considered an erased page.
 *
 * Since "uncorrectable ECC errors" do not occur except for either an
 * erased page or in the case of an actual error, this solution would
 * work.
 *
 */
static int msm_nand_is_erased_page(struct mtd_info *mtd, loff_t from,
			     struct mtd_oob_ops *ops,
			     struct msm_nand_rw_params *rw_params,
			     bool *erased_page)
{
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	uint32_t cwperpage = (mtd->writesize >> 9);
	int err, submitted_num_desc = 0;
	uint32_t n = 0, num_zero_bits = 0, total_ecc_byte_cnt;
	struct msm_nand_rw_reg_data data;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;
	struct mtd_oob_ops raw_ops;

	/*
	 * The following 6 commands will be sent only once for the first
	 * codeword (CW) - addr0, addr1, dev0_cfg0, dev0_cfg1,
	 * dev0_ecc_cfg, ebi2_ecc_buf_cfg. The following 6 commands will
	 * be sent for every CW - flash, read_location_0, read_location_1,
	 * exec, flash_status and buffer_status.
	 */
	struct msm_nand_rw_cmd_desc *cmd_list = NULL;
	uint32_t cw_desc_cnt = 0;
	struct {
		struct sps_transfer xfer;
		struct sps_iovec cmd_iovec[MAX_DESC];
		struct {
			uint32_t count;
			struct msm_nand_cmd_setup_desc setup_desc;
			struct msm_nand_cmd_cw_desc cw_desc[MAX_DESC - 1];
		} cmd_list;
		struct {
			uint32_t flash_status;
			uint32_t buffer_status;
			uint32_t erased_cw_status;
		} result[MAX_CW_PER_PAGE];
	} *dma_buffer;
	uint8_t *ecc, *ecc_temp;

	pr_debug("========================================================\n");
	total_ecc_byte_cnt = (chip->ecc_parity_bytes * cwperpage);
	memcpy(&raw_ops, ops, sizeof(struct mtd_oob_ops));
	raw_ops.mode = MTD_OPS_RAW;
	ecc = kzalloc(total_ecc_byte_cnt, GFP_KERNEL);
	if (!ecc)
		return -ENOMEM;

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer(
					chip, sizeof(*dma_buffer))));

	memset(&data, 0, sizeof(struct msm_nand_rw_reg_data));
	msm_nand_update_rw_reg_data(chip, &raw_ops, rw_params, &data);
	cmd_list = (struct msm_nand_rw_cmd_desc *)&dma_buffer->cmd_list;

	/* map the ecc for dma operations */
	rw_params->ecc_dma_addr_curr = rw_params->ecc_dma_addr =
		dma_map_single(chip->dev, ecc, total_ecc_byte_cnt,
				DMA_FROM_DEVICE);

	data.addr0 = (rw_params->page << 16) | rw_params->oob_col;
	data.addr1 = (rw_params->page >> 16) & 0xff;
	for (n = rw_params->start_sector; n < cwperpage; n++) {
		struct sps_command_element *curr_ce, *start_ce;

		dma_buffer->result[n].flash_status = 0xeeeeeeee;
		dma_buffer->result[n].buffer_status = 0xeeeeeeee;
		dma_buffer->result[n].erased_cw_status = 0xeeeeee00;

		msm_nand_prep_rw_cmd_desc(&raw_ops, rw_params, &data, info,
				n, cmd_list, &cw_desc_cnt,
				chip->ecc_parity_bytes);

		start_ce = &cmd_list->cw_desc[cw_desc_cnt].ce[0];
		curr_ce = start_ce;
		cmd_list->cw_desc[cw_desc_cnt].flags = CMD;
		if (n == (cwperpage - 1))
			cmd_list->cw_desc[cw_desc_cnt].flags |=
				INT_UNLCK;
		cmd_list->count++;

		msm_nand_prep_ce(curr_ce, MSM_NAND_FLASH_STATUS(info),
				READ, msm_virt_to_dma(chip,
				&dma_buffer->result[n].flash_status));
		curr_ce++;

		msm_nand_prep_ce(curr_ce, MSM_NAND_BUFFER_STATUS(info),
				READ, msm_virt_to_dma(chip,
				&dma_buffer->result[n].buffer_status));
		curr_ce++;

		msm_nand_prep_ce(curr_ce,
				MSM_NAND_ERASED_CW_DETECT_STATUS(info),
				READ, msm_virt_to_dma(chip,
				&dma_buffer->result[n].erased_cw_status));
		curr_ce++;
		cmd_list->cw_desc[cw_desc_cnt++].num_ce = curr_ce -
			start_ce;
	}

	dma_buffer->xfer.iovec_count = cmd_list->count;
	dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
	dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
			&dma_buffer->cmd_iovec);
	iovec = dma_buffer->xfer.iovec;

	iovec->addr =  msm_virt_to_dma(chip,
			&cmd_list->setup_desc.ce[0]);
	iovec->size = sizeof(struct sps_command_element) *
		cmd_list->setup_desc.num_ce;
	iovec->flags = cmd_list->setup_desc.flags;
	iovec++;
	for (n = 0; n < (cmd_list->count - 1); n++) {
		iovec->addr =  msm_virt_to_dma(chip,
				&cmd_list->cw_desc[n].ce[0]);
		iovec->size = sizeof(struct sps_command_element) *
			cmd_list->cw_desc[n].num_ce;
		iovec->flags = cmd_list->cw_desc[n].flags;
		iovec++;
	}
	mutex_lock(&info->lock);
	err = msm_nand_get_device(chip->dev);
	if (err)
		goto unlock_mutex;
	/* Submit data descriptors */
	for (n = rw_params->start_sector; n < cwperpage; n++) {
		err = msm_nand_submit_rw_data_desc(&raw_ops,
				rw_params, info, n,
				chip->ecc_parity_bytes);
		if (err) {
			pr_err("Failed to submit data descs %d\n", err);
			panic("error in nand driver\n");
			goto put_dev;
		}
	}
	submitted_num_desc = cwperpage - rw_params->start_sector;

	/* Submit command descriptors */
	err =  sps_transfer(info->sps.cmd_pipe.handle,
			&dma_buffer->xfer);
	if (err) {
		pr_err("Failed to submit commands %d\n", err);
		goto put_dev;
	}
	err = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count,
					submitted_num_desc, 0, 0, &iovec_temp);
	if (err)
		goto put_dev;

	err = msm_nand_put_device(chip->dev);
	mutex_unlock(&info->lock);
	if (err)
		goto free_dma;

	pr_debug("addr0: 0x%08x, addr1: 0x%08x\n", data.addr0, data.addr1);
	for (n = rw_params->start_sector; n < cwperpage; n++)
		pr_debug("cw %d: flash_sts %x buffr_sts %x, erased_cw_status: %x\n",
				n, dma_buffer->result[n].flash_status,
				dma_buffer->result[n].buffer_status,
				dma_buffer->result[n].erased_cw_status);

	goto free_dma;
put_dev:
	msm_nand_put_device(chip->dev);
unlock_mutex:
	mutex_unlock(&info->lock);
free_dma:
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
	/* umap ecc dma memory */
	dma_unmap_single(chip->dev, rw_params->ecc_dma_addr,
			total_ecc_byte_cnt, DMA_FROM_DEVICE);
	/* check for bit flips in ecc data */
	ecc_temp = ecc;
	for (n = rw_params->start_sector; n < cwperpage; n++) {
		int last_pos = 0, next_pos = 0;
		int ecc_bytes_percw_in_bits = (chip->ecc_parity_bytes * 8);

		do {
			last_pos = find_next_zero_bit((void *)ecc_temp,
					ecc_bytes_percw_in_bits, next_pos);

			if (last_pos < ecc_bytes_percw_in_bits)
				num_zero_bits++;

			if (num_zero_bits > 4) {
				*erased_page = false;
				goto free_mem;
			}

			next_pos = last_pos + 1;
		} while (last_pos < ecc_bytes_percw_in_bits);

		num_zero_bits = last_pos = next_pos = 0;
		ecc_temp += chip->ecc_parity_bytes;
	}

	if ((n == cwperpage) && (num_zero_bits <= 4))
		*erased_page = true;
free_mem:
	kfree(ecc);
	pr_debug("========================================================\n");
	return err;
}

/*
 * Function that gets called from upper layers such as MTD/YAFFS2 to read a
 * page with main or/and spare data.
 */
static int msm_nand_read_oob(struct mtd_info *mtd, loff_t from,
			     struct mtd_oob_ops *ops)
{
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	struct flash_identification *flash_dev = &info->flash_dev;
	uint32_t cwperpage = (mtd->writesize >> 9);
	int err, pageerr = 0, rawerr = 0, submitted_num_desc = 0;
	uint32_t n = 0, pages_read = 0;
	uint32_t ecc_errors = 0, total_ecc_errors = 0, ecc_capability;
	struct msm_nand_rw_params rw_params;
	struct msm_nand_rw_reg_data data;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;
	bool erased_page;
	uint64_t fix_data_in_pages = 0;

	/*
	 * The following 6 commands will be sent only once for the first
	 * codeword (CW) - addr0, addr1, dev0_cfg0, dev0_cfg1,
	 * dev0_ecc_cfg, ebi2_ecc_buf_cfg. The following 6 commands will
	 * be sent for every CW - flash, read_location_0, read_location_1,
	 * exec, flash_status and buffer_status.
	 */
	struct {
		struct sps_transfer xfer;
		struct sps_iovec cmd_iovec[MAX_DESC];
		struct {
			uint32_t count;
			struct msm_nand_cmd_setup_desc setup_desc;
			struct msm_nand_cmd_cw_desc cw_desc[MAX_DESC - 1];
		} cmd_list;
		struct {
			uint32_t flash_status;
			uint32_t buffer_status;
			uint32_t erased_cw_status;
		} result[MAX_CW_PER_PAGE];
	} *dma_buffer;
	struct msm_nand_rw_cmd_desc *cmd_list = NULL;

	memset(&rw_params, 0, sizeof(struct msm_nand_rw_params));
	err = msm_nand_validate_mtd_params(mtd, true, from, ops, &rw_params);
	if (err)
		goto validate_mtd_params_failed;

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer(
			    chip, sizeof(*dma_buffer))));

	rw_params.oob_col = rw_params.start_sector * chip->cw_size;
	if (chip->cfg1 & (1 << WIDE_FLASH))
		rw_params.oob_col >>= 1;

	memset(&data, 0, sizeof(struct msm_nand_rw_reg_data));
	msm_nand_update_rw_reg_data(chip, ops, &rw_params, &data);
	cmd_list = (struct msm_nand_rw_cmd_desc *)&dma_buffer->cmd_list;

	ecc_capability = flash_dev->ecc_capability;

	while (rw_params.page_count-- > 0) {
		uint32_t cw_desc_cnt = 0;

		erased_page = false;
		data.addr0 = (rw_params.page << 16) | rw_params.oob_col;
		data.addr1 = (rw_params.page >> 16) & 0xff;

		if (ops->len <= ONE_CODEWORD_SIZE)
			cwperpage = 1;

		for (n = rw_params.start_sector; n < cwperpage; n++) {
			struct sps_command_element *curr_ce, *start_ce;

			dma_buffer->result[n].flash_status = 0xeeeeeeee;
			dma_buffer->result[n].buffer_status = 0xeeeeeeee;
			dma_buffer->result[n].erased_cw_status = 0xeeeeee00;

			msm_nand_prep_rw_cmd_desc(ops, &rw_params, &data, info,
					n, cmd_list, &cw_desc_cnt, 0);

			start_ce = &cmd_list->cw_desc[cw_desc_cnt].ce[0];
			curr_ce = start_ce;
			cmd_list->cw_desc[cw_desc_cnt].flags = CMD;
			if (n == (cwperpage - 1))
				cmd_list->cw_desc[cw_desc_cnt].flags |=
								INT_UNLCK;
			cmd_list->count++;

			msm_nand_prep_ce(curr_ce, MSM_NAND_FLASH_STATUS(info),
				READ, msm_virt_to_dma(chip,
					&dma_buffer->result[n].flash_status));
			curr_ce++;

			msm_nand_prep_ce(curr_ce, MSM_NAND_BUFFER_STATUS(info),
				READ, msm_virt_to_dma(chip,
					&dma_buffer->result[n].buffer_status));
			curr_ce++;

			msm_nand_prep_ce(curr_ce,
				MSM_NAND_ERASED_CW_DETECT_STATUS(info),
				READ, msm_virt_to_dma(chip,
				&dma_buffer->result[n].erased_cw_status));
			curr_ce++;
			cmd_list->cw_desc[cw_desc_cnt++].num_ce = curr_ce -
				start_ce;
		}

		dma_buffer->xfer.iovec_count = cmd_list->count;
		dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
		dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
						&dma_buffer->cmd_iovec);
		iovec = dma_buffer->xfer.iovec;

		iovec->addr =  msm_virt_to_dma(chip,
				&cmd_list->setup_desc.ce[0]);
		iovec->size = sizeof(struct sps_command_element) *
			cmd_list->setup_desc.num_ce;
		iovec->flags = cmd_list->setup_desc.flags;
		iovec++;
		for (n = 0; n < (cmd_list->count - 1); n++) {
			iovec->addr =  msm_virt_to_dma(chip,
						&cmd_list->cw_desc[n].ce[0]);
			iovec->size = sizeof(struct sps_command_element) *
						cmd_list->cw_desc[n].num_ce;
			iovec->flags = cmd_list->cw_desc[n].flags;
			iovec++;
		}
		mutex_lock(&info->lock);
		err = msm_nand_get_device(chip->dev);
		if (err)
			goto unlock_mutex;
		/* Submit data descriptors */
		for (n = rw_params.start_sector; n < cwperpage; n++) {
			err = msm_nand_submit_rw_data_desc(ops,
						&rw_params, info, n, 0);
			if (err) {
				pr_err("Failed to submit data descs %d\n", err);
				panic("error in nand driver\n");
				goto put_dev;
			}
		}

		if (ops->mode == MTD_OPS_RAW) {
			submitted_num_desc = cwperpage - rw_params.start_sector;
		} else if (ops->mode == MTD_OPS_AUTO_OOB) {
			if (ops->datbuf)
				submitted_num_desc = cwperpage -
					rw_params.start_sector;
			if (ops->oobbuf)
				submitted_num_desc++;
		}

		/* Submit command descriptors */
		err =  sps_transfer(info->sps.cmd_pipe.handle,
				&dma_buffer->xfer);
		if (err) {
			pr_err("Failed to submit commands %d\n", err);
			goto put_dev;
		}
		err = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count,
					submitted_num_desc, 0, 0, &iovec_temp);
		if (err)
			goto put_dev;

		err = msm_nand_put_device(chip->dev);
		mutex_unlock(&info->lock);
		if (err)
			goto free_dma;
		/* Check for flash status errors */
		pageerr = rawerr = 0;
		for (n = rw_params.start_sector; n < cwperpage; n++) {
			if (dma_buffer->result[n].flash_status & (FS_OP_ERR |
					FS_MPU_ERR)) {
				rawerr = -EIO;
				/*
				 * Check if ECC error was due to an erased
				 * codeword. If so, ignore the error.
				 *
				 * NOTE: There is a bug in erased page
				 * detection hardware block when reading
				 * only spare data. In order to work around
				 * this issue, instead of using PAGE_ALL_ERASED
				 * bit to check for whether a whole page is
				 * erased or not, we use CODEWORD_ALL_ERASED
				 * and  CODEWORD_ERASED bits together and check
				 * each codeword that has FP_OP_ERR bit set is
				 * an erased codeword or not.
				 */
				if ((dma_buffer->result[n].erased_cw_status &
					ERASED_CW) == ERASED_CW) {
					/*
					 * At least one code word is detected
					 * as an erased code word.
					 */
					pr_debug("erased codeword detected - ignore ecc error\n");
					continue;
				}
				pageerr = rawerr;
				break;
			}
		}
		/* check for uncorrectable errors */
		if (pageerr) {
			for (n = rw_params.start_sector; n < cwperpage; n++) {
				if (dma_buffer->result[n].buffer_status &
					BS_UNCORRECTABLE_BIT) {
					/*
					 * Check if page is actually
					 * erased or not.
					 */
					err = msm_nand_is_erased_page(mtd,
							from, ops,
							&rw_params,
							&erased_page);
					if (err)
						goto free_dma;
					if (!erased_page) {
						mtd->ecc_stats.failed++;
						pageerr = -EBADMSG;
						break;
					}
					pageerr = 0;
					pr_debug("Uncorrectable ECC errors detected on an erased page and has been fixed.\n");
					break;
				}
			}
		}

		if (rawerr && !pageerr && erased_page) {
			/*
			 * This means an erased page had bit flips and now
			 * those bit-flips need to be cleared in the data
			 * being sent to upper layers. This will keep track
			 * of those pages and at the end, the data will be
			 * fixed before this function returns.
			 * Note that a whole page worth of data will be fixed
			 * and this will only handle about 64 pages being read
			 * at a time i.e. one erase block worth of pages.
			 */
			fix_data_in_pages |= BIT(rw_params.page_count);
		}
		/* check for correctable errors */
		if (!rawerr) {
			for (n = rw_params.start_sector; n < cwperpage; n++) {
				ecc_errors =
				    dma_buffer->result[n].buffer_status
				    & BS_CORRECTABLE_ERR_MSK;
				if (ecc_errors) {
					total_ecc_errors += ecc_errors;
					mtd->ecc_stats.corrected += ecc_errors;
				}
			}
		}
		if (pageerr && (pageerr != -EUCLEAN || err == 0))
			err = pageerr;

		if (rawerr && !pageerr) {
			pr_debug("%llx %zx %zx empty page\n",
			       (loff_t)rw_params.page * mtd->writesize,
			       ops->len, ops->ooblen);
		} else {
			for (n = rw_params.start_sector; n < cwperpage; n++)
				pr_debug("cw %d: flash_sts %x buffr_sts %x, erased_cw_status: %x, pageerr: %d, rawerr: %d\n",
				n, dma_buffer->result[n].flash_status,
				dma_buffer->result[n].buffer_status,
				dma_buffer->result[n].erased_cw_status,
				pageerr, rawerr);
		}
		if (err && err != -EUCLEAN && err != -EBADMSG)
			goto free_dma;
		pages_read++;
		rw_params.page++;
	}
	goto free_dma;
put_dev:
	msm_nand_put_device(chip->dev);
unlock_mutex:
	mutex_unlock(&info->lock);
free_dma:
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
	if (ops->oobbuf)
		dma_unmap_page(chip->dev, rw_params.oob_dma_addr,
				 ops->ooblen, DMA_FROM_DEVICE);
	if (ops->datbuf)
		dma_unmap_page(chip->dev, rw_params.data_dma_addr,
				 ops->len, DMA_BIDIRECTIONAL);
	/*
	 * If there were any erased pages detected with ECC errors, then
	 * it is most likely that the data is not all 0xff. So memset that
	 * page to all 0xff.
	 */
	while (fix_data_in_pages) {
		int temp_page = 0, oobsize = rw_params.cwperpage << 2;
		int count = 0, offset = 0;

		temp_page = fix_data_in_pages & BIT_MASK(0);
		fix_data_in_pages = fix_data_in_pages >> 1;
		count++;

		if (!temp_page)
			continue;

		offset = (count - 1) * mtd->writesize;
		if (ops->datbuf)
			memset((ops->datbuf + offset), 0xff, mtd->writesize);

		offset = (count - 1) * oobsize;
		if (ops->oobbuf)
			memset(ops->oobbuf + offset, 0xff, oobsize);
	}
validate_mtd_params_failed:
	if (ops->mode != MTD_OPS_RAW)
		if (ops->len <= ONE_CODEWORD_SIZE)
			ops->retlen = ONE_CODEWORD_SIZE;
		else
			ops->retlen = mtd->writesize * pages_read;
	else
		ops->retlen = (mtd->writesize +  mtd->oobsize) * pages_read;
	ops->oobretlen = ops->ooblen - rw_params.oob_len_data;
	if (err)
		pr_err("0x%llx datalen 0x%zx ooblen %zx err %d corrected %d\n",
		       from, ops->datbuf ? ops->len : 0, ops->ooblen, err,
		       total_ecc_errors);
	pr_debug("ret %d, retlen %zu oobretlen %zu\n",
			err, ops->retlen, ops->oobretlen);

	pr_debug("========================================================\n");
	return err;
}

/**
 * msm_nand_read_partial_page() - read partial page
 * @mtd: pointer to mtd info
 * @from: start address of the page
 * @ops: pointer to mtd_oob_ops
 *
 * Reads a page into a bounce buffer and copies the required
 * number of bytes to actual buffer. The pages that are aligned
 * do not use bounce buffer.
 */
static int msm_nand_read_partial_page(struct mtd_info *mtd,
		loff_t from, struct mtd_oob_ops *ops)
{
	int err = 0;
	unsigned char *actual_buf;
	unsigned char *bounce_buf;
	loff_t aligned_from;
	loff_t offset;
	size_t len;
	size_t actual_len, ret_len;
	int is_euclean = 0;
	int is_ebadmsg = 0;
	struct msm_nand_info *info = mtd->priv;

	actual_len = ops->len;
	ret_len = 0;
	actual_buf = ops->datbuf;

	bounce_buf = kmalloc(mtd->writesize, GFP_KERNEL);
	if (!bounce_buf) {
		err = -ENOMEM;
		goto out;
	}

	/* Get start address of page to read from */
	ops->len = mtd->writesize;
	offset = from & (mtd->writesize - 1);
	aligned_from = from - offset;

	for (;;) {
		bool no_copy = false;

		len = mtd->writesize - offset;
		if (len > actual_len)
			len = actual_len;

		if (offset == 0 && len == mtd->writesize)
			no_copy = true;

		if (!virt_addr_valid(actual_buf) &&
				!is_buffer_in_page(actual_buf, ops->len))
			no_copy = false;

		ops->datbuf = no_copy ? actual_buf : bounce_buf;

		/*
		 * Do a Pagescope read only if PAGE_SCOPE_READ is enabled
		 * and request length is greater than codeword size or
		 * the page offset is not aligned to start of the page.
		 */
		if ((info->nand_chip.caps & MSM_NAND_CAP_PAGE_SCOPE_READ) &&
				((len > ONE_CODEWORD_SIZE) || (offset != 0)))
			len > mtd->writesize ?
				(err = msm_nand_read_multipage(mtd, aligned_from, ops)) :
					(err = msm_nand_read_pagescope(mtd, aligned_from, ops));
		else {
			if ((len <= ONE_CODEWORD_SIZE) && (offset == 0))
				ops->len = ONE_CODEWORD_SIZE;
			err = msm_nand_read_oob(mtd, aligned_from, ops);
		}
		if (err == -EUCLEAN) {
			is_euclean = 1;
			err = 0;
		}

		if (err == -EBADMSG) {
			is_ebadmsg = 1;
			err = 0;
		}

		if (err < 0) {
			/* Clear previously set EUCLEAN / EBADMSG */
			is_euclean = 0;
			is_ebadmsg = 0;
			ret_len = ops->retlen;
			break;
		}

		if (!no_copy)
			memcpy(actual_buf, bounce_buf + offset, len);

		actual_len -= len;
		ret_len += len;

		if (actual_len == 0)
			break;

		actual_buf += len;
		offset = 0;
		aligned_from += mtd->writesize;
	}

	ops->retlen = ret_len;
	kfree(bounce_buf);
out:
	if (is_euclean == 1)
		err = -EUCLEAN;

	/* Snub EUCLEAN if we also have EBADMSG */
	if (is_ebadmsg == 1)
		err = -EBADMSG;
	return err;
}

/*
 * Function that gets called from upper layers such as MTD/YAFFS2 to read a
 * page with only main data.
 */
static int msm_nand_read(struct mtd_info *mtd, loff_t from, size_t len,
	      size_t *retlen, u_char *buf)
{
	int ret;
	int is_euclean = 0;
	int is_ebadmsg = 0;
	struct mtd_oob_ops ops;
	unsigned char *bounce_buf = NULL;
	struct msm_nand_info *info = mtd->priv;

	ops.mode = MTD_OPS_AUTO_OOB;
	ops.retlen = 0;
	ops.ooblen = 0;
	ops.oobbuf = NULL;
	*retlen = 0;

	if (!(from & (mtd->writesize - 1)) && !(len % mtd->writesize)) {
		/*
		 * Handle reading of large size read buffer in vmalloc
		 * address space that does not fit in an MMU page.
		 */
		if (!virt_addr_valid(buf) && !is_buffer_in_page(buf, len)) {
			ops.len = mtd->writesize;

			bounce_buf = kmalloc(ops.len, GFP_KERNEL);
			if (!bounce_buf) {
				ret = -ENOMEM;
				goto out;
			}

			for (;;) {
				bool no_copy = false;

				if (!is_buffer_in_page(buf, ops.len)) {
					memcpy(bounce_buf, buf, ops.len);
					ops.datbuf = (uint8_t *) bounce_buf;
				} else {
					ops.datbuf = (uint8_t *) buf;
					no_copy = true;
				}
				if (info->nand_chip.caps &
						MSM_NAND_CAP_PAGE_SCOPE_READ) {
					ret = msm_nand_read_pagescope(mtd,
								from, &ops);
				} else {
					ret = msm_nand_read_oob(mtd,
							from, &ops);
				}
				if (ret == -EUCLEAN) {
					is_euclean = 1;
					ret = 0;
				}
				if (ret == -EBADMSG) {
					is_ebadmsg = 1;
					ret = 0;
				}
				if (ret < 0) {
					/* Clear previously set errors */
					is_euclean = 0;
					is_ebadmsg = 0;
					break;
				}


				if (!no_copy)
					memcpy(buf, bounce_buf, ops.retlen);

				len -= ops.retlen;
				*retlen += ops.retlen;
				if (len == 0)
					break;
				buf += ops.retlen;
				from += ops.retlen;

				if (len < mtd->writesize) {
					ops.len = len;
					ops.datbuf = buf;
					ret = msm_nand_read_partial_page(
						mtd, from, &ops);
					*retlen += ops.retlen;
					break;
				}
			}
			kfree(bounce_buf);
		} else {
			ops.len = len;
			ops.datbuf = (uint8_t *)buf;
			if (info->nand_chip.caps &
					MSM_NAND_CAP_PAGE_SCOPE_READ) {
				if (ops.len > mtd->writesize)
					ret = msm_nand_read_multipage(mtd, from, &ops);
				else
					ret = msm_nand_read_pagescope(mtd, from, &ops);
			} else {
				ret =  msm_nand_read_oob(mtd, from, &ops);
			}
			*retlen = ops.retlen;
		}
	} else {
		ops.len = len;
		ops.datbuf = (uint8_t *)buf;
		ret = msm_nand_read_partial_page(mtd, from, &ops);
		*retlen = ops.retlen;
	}
out:
	if (is_euclean == 1)
		ret = -EUCLEAN;

	/* Snub EUCLEAN if we also have EBADMSG */
	if (is_ebadmsg == 1)
		ret = -EBADMSG;

	return ret;
}

/*
 * Function that gets called from upper layers such as MTD/YAFFS2 to write a
 * page with both main and spare data.
 */
static int msm_nand_write_oob(struct mtd_info *mtd, loff_t to,
				struct mtd_oob_ops *ops)
{
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	uint32_t cwperpage = (mtd->writesize >> 9);
	uint32_t n, flash_sts, pages_written = 0;
	int err = 0, submitted_num_desc = 0;
	struct msm_nand_rw_params rw_params;
	struct msm_nand_rw_reg_data data;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;

	/*
	 * The following 7 commands will be sent only once :
	 * For first codeword (CW) - addr0, addr1, dev0_cfg0, dev0_cfg1,
	 * dev0_ecc_cfg, ebi2_ecc_buf_cfg.
	 * For last codeword (CW) - read_status(write)
	 *
	 * The following 4 commands will be sent for every CW :
	 * flash, exec, flash_status (read), flash_status (write).
	 */
	struct {
		struct sps_transfer xfer;
		struct sps_iovec cmd_iovec[MAX_DESC + 1];
		struct {
			uint32_t count;
			struct msm_nand_cmd_setup_desc setup_desc;
			struct msm_nand_cmd_cw_desc cw_desc[MAX_DESC];
		} cmd_list;
		struct {
			uint32_t flash_status;
		} data[MAX_CW_PER_PAGE];
	} *dma_buffer;
	struct msm_nand_rw_cmd_desc *cmd_list = NULL;

	memset(&rw_params, 0, sizeof(struct msm_nand_rw_params));
	err = msm_nand_validate_mtd_params(mtd, false, to, ops, &rw_params);
	if (err)
		goto validate_mtd_params_failed;

	wait_event(chip->dma_wait_queue, (dma_buffer =
			msm_nand_get_dma_buffer(chip, sizeof(*dma_buffer))));

	memset(&data, 0, sizeof(struct msm_nand_rw_reg_data));
	msm_nand_update_rw_reg_data(chip, ops, &rw_params, &data);
	cmd_list = (struct msm_nand_rw_cmd_desc *)&dma_buffer->cmd_list;

	while (rw_params.page_count-- > 0) {
		uint32_t cw_desc_cnt = 0;
		struct sps_command_element *curr_ce, *start_ce;

		data.addr0 = (rw_params.page << 16);
		data.addr1 = (rw_params.page >> 16) & 0xff;

		for (n = 0; n < cwperpage ; n++) {
			dma_buffer->data[n].flash_status = 0xeeeeeeee;

			msm_nand_prep_rw_cmd_desc(ops, &rw_params, &data, info,
					n, cmd_list, &cw_desc_cnt, 0);

			curr_ce = &cmd_list->cw_desc[cw_desc_cnt].ce[0];
			cmd_list->cw_desc[cw_desc_cnt].flags = CMD;
			cmd_list->count++;

			msm_nand_prep_ce(curr_ce, MSM_NAND_FLASH_STATUS(info),
				READ, msm_virt_to_dma(chip,
					&dma_buffer->data[n].flash_status));
			cmd_list->cw_desc[cw_desc_cnt++].num_ce = 1;
		}

		start_ce = &cmd_list->cw_desc[cw_desc_cnt].ce[0];
		curr_ce = start_ce;
		cmd_list->cw_desc[cw_desc_cnt].flags = CMD_INT_UNLCK;
		cmd_list->count++;
		msm_nand_prep_ce(curr_ce, MSM_NAND_FLASH_STATUS(info),
				WRITE, data.clrfstatus);
		curr_ce++;

		msm_nand_prep_ce(curr_ce, MSM_NAND_READ_STATUS(info),
				WRITE, data.clrrstatus);
		curr_ce++;
		cmd_list->cw_desc[cw_desc_cnt++].num_ce = curr_ce - start_ce;

		dma_buffer->xfer.iovec_count = cmd_list->count;
		dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
		dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
						&dma_buffer->cmd_iovec);
		iovec = dma_buffer->xfer.iovec;

		iovec->addr =  msm_virt_to_dma(chip,
				&cmd_list->setup_desc.ce[0]);
		iovec->size = sizeof(struct sps_command_element) *
					cmd_list->setup_desc.num_ce;
		iovec->flags = cmd_list->setup_desc.flags;
		iovec++;
		for (n = 0; n < (cmd_list->count - 1); n++) {
			iovec->addr =  msm_virt_to_dma(chip,
					&cmd_list->cw_desc[n].ce[0]);
			iovec->size = sizeof(struct sps_command_element) *
					cmd_list->cw_desc[n].num_ce;
			iovec->flags = cmd_list->cw_desc[n].flags;
			iovec++;
		}
		mutex_lock(&info->lock);
		err = msm_nand_get_device(chip->dev);
		if (err)
			goto unlock_mutex;
		/* Submit data descriptors */
		for (n = 0; n < cwperpage; n++) {
			err = msm_nand_submit_rw_data_desc(ops,
						&rw_params, info, n, 0);
			if (err) {
				pr_err("Failed to submit data descs %d\n", err);
				panic("Error in nand driver\n");
				goto put_dev;
			}
		}

		if (ops->mode == MTD_OPS_RAW) {
			submitted_num_desc = n;
		} else if (ops->mode == MTD_OPS_AUTO_OOB) {
			if (ops->datbuf)
				submitted_num_desc = n;
			if (ops->oobbuf)
				submitted_num_desc++;
		}

		/* Submit command descriptors */
		err =  sps_transfer(info->sps.cmd_pipe.handle,
				&dma_buffer->xfer);
		if (err) {
			pr_err("Failed to submit commands %d\n", err);
			goto put_dev;
		}
		err = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count,
						0, 0, submitted_num_desc, &iovec_temp);
		if (err)
			goto put_dev;

		err = msm_nand_put_device(chip->dev);
		mutex_unlock(&info->lock);
		if (err)
			goto free_dma;

		for (n = 0; n < cwperpage; n++)
			pr_debug("write pg %d: flash_status[%d] = %x\n",
				rw_params.page, n,
				dma_buffer->data[n].flash_status);

		/*  Check for flash status errors */
		for (n = 0; n < cwperpage; n++) {
			flash_sts = dma_buffer->data[n].flash_status;
			if (flash_sts & (FS_OP_ERR | FS_MPU_ERR)) {
				pr_err("MPU/OP err (0x%x) set\n", flash_sts);
				err = -EIO;
				goto free_dma;
			}
			if (n == (cwperpage - 1)) {
				if (!(flash_sts & FS_DEVICE_WP) ||
					(flash_sts & FS_DEVICE_STS_ERR)) {
					pr_err("Dev sts err 0x%x\n", flash_sts);
					err = -EIO;
					goto free_dma;
				}
			}
		}
		pages_written++;
		rw_params.page++;
	}
	goto free_dma;
put_dev:
	msm_nand_put_device(chip->dev);
unlock_mutex:
	mutex_unlock(&info->lock);
free_dma:
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
	if (ops->oobbuf)
		dma_unmap_page(chip->dev, rw_params.oob_dma_addr,
				 ops->ooblen, DMA_TO_DEVICE);
	if (ops->datbuf)
		dma_unmap_page(chip->dev, rw_params.data_dma_addr,
				ops->len, DMA_TO_DEVICE);
validate_mtd_params_failed:
	if (ops->mode != MTD_OPS_RAW)
		ops->retlen = mtd->writesize * pages_written;
	else
		ops->retlen = (mtd->writesize + mtd->oobsize) * pages_written;

	ops->oobretlen = ops->ooblen - rw_params.oob_len_data;
	if (err)
		pr_err("to %llx datalen %zx ooblen %zx failed with err %d\n",
		       to, ops->len, ops->ooblen, err);
	pr_debug("ret %d, retlen %zu oobretlen %zu\n",
			err, ops->retlen, ops->oobretlen);

	pr_debug("================================================\n");
	return err;
}

/*
 * Function that gets called from upper layers such as MTD/YAFFS2 to write a
 * page with only main data.
 */
static int msm_nand_write(struct mtd_info *mtd, loff_t to, size_t len,
			  size_t *retlen, const u_char *buf)
{
	int ret;
	struct mtd_oob_ops ops;
	unsigned char *bounce_buf = NULL;

	ops.mode = MTD_OPS_AUTO_OOB;
	ops.retlen = 0;
	ops.ooblen = 0;
	ops.oobbuf = NULL;

	/* partial page writes are not supported */
	if ((to & (mtd->writesize - 1)) || (len % mtd->writesize)) {
		ret = -EINVAL;
		*retlen = ops.retlen;
		pr_err("%s: partial page writes are not supported\n", __func__);
		goto out;
	}

	/*
	 * Handle writing of large size write buffer in vmalloc
	 * address space that does not fit in an MMU page.
	 */
	if (!virt_addr_valid(buf) && !is_buffer_in_page(buf, len)) {
		ops.len = mtd->writesize;

		bounce_buf = kmalloc(ops.len, GFP_KERNEL);
		if (!bounce_buf) {
			ret = -ENOMEM;
			goto out;
		}

		for (;;) {
			if (!is_buffer_in_page(buf, ops.len)) {
				memcpy(bounce_buf, buf, ops.len);
				ops.datbuf = (uint8_t *) bounce_buf;
			} else {
				ops.datbuf = (uint8_t *) buf;
			}
			ret = msm_nand_write_oob(mtd, to, &ops);
			if (ret < 0)
				break;

			len -= mtd->writesize;
			*retlen += mtd->writesize;
			if (len == 0)
				break;

			buf += mtd->writesize;
			to += mtd->writesize;
		}
		kfree(bounce_buf);
	} else {
		ops.len = len;
		ops.datbuf = (uint8_t *)buf;
		ret =  msm_nand_write_oob(mtd, to, &ops);
		*retlen = ops.retlen;
	}
out:
	return ret;
}

/*
 * Structure that contains NANDc register data for commands required
 * for Erase operation.
 */
struct msm_nand_erase_reg_data {
	struct msm_nand_common_cfgs cfg;
	uint32_t exec;
	uint32_t flash_status;
	uint32_t clrfstatus;
	uint32_t clrrstatus;
};

/*
 * Function that gets called from upper layers such as MTD/YAFFS2 to erase a
 * block within NAND device.
 */
#define ERASE_CMDS 9
static int msm_nand_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	int i = 0, err = 0;
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	uint32_t page = 0;
	struct msm_nand_sps_cmd *cmd, *curr_cmd;
	struct msm_nand_erase_reg_data data;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;

	/*
	 * The following 9 commands are required to erase a page -
	 * flash, addr0, addr1, cfg0, cfg1, exec, flash_status(read),
	 * flash_status(write), read_status.
	 */
	struct {
		struct sps_transfer xfer;
		struct sps_iovec cmd_iovec[ERASE_CMDS];
		struct msm_nand_sps_cmd cmd[ERASE_CMDS];
		uint32_t flash_status;
	} *dma_buffer;

	if (mtd->writesize == PAGE_SIZE_2K)
		page = instr->addr >> 11;

	if (mtd->writesize == PAGE_SIZE_4K)
		page = instr->addr >> 12;

	if (instr->addr & (mtd->erasesize - 1)) {
		pr_err("unsupported erase address, 0x%llx\n", instr->addr);
		err = -EINVAL;
		goto out;
	}
	if (instr->len != mtd->erasesize) {
		pr_err("unsupported erase len, %lld\n", instr->len);
		err = -EINVAL;
		goto out;
	}

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer(
			    chip, sizeof(*dma_buffer))));
	cmd = dma_buffer->cmd;

	memset(&data, 0, sizeof(struct msm_nand_erase_reg_data));
	data.cfg.cmd = MSM_NAND_CMD_BLOCK_ERASE;
	data.cfg.addr0 = page;
	data.cfg.addr1 = 0;
	data.cfg.cfg0 = chip->cfg0 & (~(7 << CW_PER_PAGE));
	data.cfg.cfg1 = chip->cfg1;
	data.exec = 1;
	dma_buffer->flash_status = 0xeeeeeeee;
	data.clrfstatus = MSM_NAND_RESET_FLASH_STS;
	data.clrrstatus = MSM_NAND_RESET_READ_STS;

	curr_cmd = cmd;
	msm_nand_prep_cfg_cmd_desc(info, data.cfg, &curr_cmd);

	cmd = curr_cmd;
	msm_nand_prep_single_desc(cmd, MSM_NAND_EXEC_CMD(info), WRITE,
			data.exec, SPS_IOVEC_FLAG_NWD);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_FLASH_STATUS(info), READ,
		msm_virt_to_dma(chip, &dma_buffer->flash_status), 0);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_FLASH_STATUS(info), WRITE,
			data.clrfstatus, 0);
	cmd++;

	msm_nand_prep_single_desc(cmd, MSM_NAND_READ_STATUS(info), WRITE,
			data.clrrstatus,
			SPS_IOVEC_FLAG_UNLOCK | SPS_IOVEC_FLAG_INT);
	cmd++;

	WARN_ON((cmd - dma_buffer->cmd) > ERASE_CMDS);
	dma_buffer->xfer.iovec_count = (cmd - dma_buffer->cmd);
	dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
	dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
					&dma_buffer->cmd_iovec);
	iovec = dma_buffer->xfer.iovec;

	for (i = 0; i < dma_buffer->xfer.iovec_count; i++) {
		iovec->addr =  msm_virt_to_dma(chip, &dma_buffer->cmd[i].ce);
		iovec->size = sizeof(struct sps_command_element);
		iovec->flags = dma_buffer->cmd[i].flags;
		iovec++;
	}
	mutex_lock(&info->lock);
	err = msm_nand_get_device(chip->dev);
	if (err)
		goto unlock_mutex;

	err =  sps_transfer(info->sps.cmd_pipe.handle, &dma_buffer->xfer);
	if (err) {
		pr_err("Failed to submit commands %d\n", err);
		goto put_dev;
	}
	err = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count, 0, 0, 0, &iovec_temp);
	if (err)
		goto put_dev;
	err = msm_nand_put_device(chip->dev);
	if (err)
		goto unlock_mutex;

	/*  Check for flash status errors */
	if (dma_buffer->flash_status & (FS_OP_ERR |
			FS_MPU_ERR | FS_DEVICE_STS_ERR)) {
		pr_err("MPU/OP/DEV err (0x%x) set\n", dma_buffer->flash_status);
		err = -EIO;
	}
	if (!(dma_buffer->flash_status & FS_DEVICE_WP)) {
		pr_err("Device is write protected\n");
		err = -EIO;
	}
	if (err) {
		pr_err("Erase failed, 0x%llx\n", instr->addr);
		instr->fail_addr = instr->addr;
	} else {
		instr->fail_addr = 0xffffffff;
	}

	goto unlock_mutex;
put_dev:
	msm_nand_put_device(chip->dev);
unlock_mutex:
	mutex_unlock(&info->lock);
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer));
out:
	return err;
}

/*
 * Structure that contains NANDc register data for commands required
 * for checking if a block is bad.
 */
struct msm_nand_blk_isbad_data {
	struct msm_nand_common_cfgs cfg;
	uint32_t ecc_bch_cfg;
	uint32_t exec;
	uint32_t read_offset;
};

/*
 * Function that gets called from upper layers such as MTD/YAFFS2 to check if
 * a block is bad. This is done by reading the first and second page within a block
 * for a NON-ONFI device and first and last page for a ONFI device and
 * checking whether the bad block byte location contains 0xFF or not. If it
 * doesn't contain 0xFF, then it is considered as bad block.
 */
#define ISBAD_CMDS 10
#define BAD_BLOCK_CHECK_PAGES 2
static int msm_nand_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	struct flash_identification *flash = &info->flash_dev;
	int i = 0, ret = 0, bad_block = 0, submitted_num_desc = 1;
	int bad_block_page = 0, bad_page_count = 0;
	uint8_t *buf;
	uint32_t page = 0, rdata, cwperpage, pages_per_block;
	struct msm_nand_sps_cmd *cmd, *curr_cmd;
	struct msm_nand_blk_isbad_data data;
	struct sps_iovec *iovec;
	struct sps_iovec iovec_temp;
	/*
	 * The following 9 commands are required to check bad block -
	 * flash, addr0, addr1, cfg0, cfg1, ecc_cfg, read_loc_0,
	 * exec, flash_status(read).
	 */
	struct {
		struct sps_transfer xfer;
		struct sps_iovec cmd_iovec[ISBAD_CMDS];
		struct msm_nand_sps_cmd cmd[ISBAD_CMDS];
		uint32_t flash_status;
	} *dma_buffer;

	if (mtd->writesize == PAGE_SIZE_2K) {
		page = ofs >> 11;
		pages_per_block = mtd->erasesize >> 11;
	}

	if (mtd->writesize == PAGE_SIZE_4K) {
		page = ofs >> 12;
		pages_per_block = mtd->erasesize >> 12;
	}

	cwperpage = (mtd->writesize >> 9);

	if (ofs > mtd->size) {
		pr_err("Invalid offset 0x%llx\n", ofs);
		bad_block = -EINVAL;
		goto out;
	}
	if (ofs & (mtd->erasesize - 1)) {
		pr_err("unsupported block address, 0x%x\n", (uint32_t)ofs);
		bad_block = -EINVAL;
		goto out;
	}

	wait_event(chip->dma_wait_queue, (dma_buffer = msm_nand_get_dma_buffer(
				chip, sizeof(*dma_buffer) + 4)));
	buf = (uint8_t *)dma_buffer + sizeof(*dma_buffer);

	do {
		memset(buf, 0, 4);
		cmd = dma_buffer->cmd;
		memset(&data, 0, sizeof(struct msm_nand_blk_isbad_data));
		data.cfg.cmd = MSM_NAND_CMD_PAGE_READ_ALL;
		data.cfg.cfg0 = chip->cfg0_raw & ~(7U << CW_PER_PAGE);
		data.cfg.cfg1 = chip->cfg1_raw;

		if (chip->cfg1 & (1 << WIDE_FLASH))
			data.cfg.addr0 = (page << 16) |
				((chip->cw_size * (cwperpage-1)) >> 1);
		else
			data.cfg.addr0 = (page << 16) |
				(chip->cw_size * (cwperpage-1));

		data.cfg.addr1 = (page >> 16) & 0xff;
		data.ecc_bch_cfg = 1 << ECC_CFG_ECC_DISABLE;
		data.exec = 1;
		data.read_offset = (mtd->writesize - (chip->cw_size * (cwperpage-1)));
		dma_buffer->flash_status = 0xeeeeeeee;

		curr_cmd = cmd;
		msm_nand_prep_cfg_cmd_desc(info, data.cfg, &curr_cmd);

		cmd = curr_cmd;
		msm_nand_prep_single_desc(cmd, MSM_NAND_DEV0_ECC_CFG(info), WRITE,
			data.ecc_bch_cfg, 0);
		cmd++;

		rdata = (data.read_offset << 0) | (4 << 16) | (1 << 31);
		msm_nand_prep_single_desc(cmd, MSM_NAND_READ_LOCATION_0(info), WRITE,
			rdata, 0);
		cmd++;

		if (chip->qpic_version >= 2) {
			msm_nand_prep_single_desc(cmd,
				MSM_NAND_READ_LOCATION_LAST_CW_0(info), WRITE,
				rdata, 0);
			cmd++;
		}

		msm_nand_prep_single_desc(cmd, MSM_NAND_EXEC_CMD(info), WRITE,
				data.exec, SPS_IOVEC_FLAG_NWD);
		cmd++;

		msm_nand_prep_single_desc(cmd, MSM_NAND_FLASH_STATUS(info), READ,
			msm_virt_to_dma(chip, &dma_buffer->flash_status),
			SPS_IOVEC_FLAG_INT | SPS_IOVEC_FLAG_UNLOCK);
		cmd++;

		WARN_ON(cmd - dma_buffer->cmd > ISBAD_CMDS);
		dma_buffer->xfer.iovec_count = (cmd - dma_buffer->cmd);
		dma_buffer->xfer.iovec = dma_buffer->cmd_iovec;
		dma_buffer->xfer.iovec_phys = msm_virt_to_dma(chip,
					&dma_buffer->cmd_iovec);
		iovec = dma_buffer->xfer.iovec;

		for (i = 0; i < dma_buffer->xfer.iovec_count; i++) {
			iovec->addr =  msm_virt_to_dma(chip, &dma_buffer->cmd[i].ce);
			iovec->size = sizeof(struct sps_command_element);
			iovec->flags = dma_buffer->cmd[i].flags;
			iovec++;
		}
		mutex_lock(&info->lock);
		ret = msm_nand_get_device(chip->dev);
		if (ret) {
			mutex_unlock(&info->lock);
			goto free_dma;
		}
		/* Submit data descriptor */
		ret = sps_transfer_one(info->sps.data_prod.handle,
				msm_virt_to_dma(chip, buf),
				4, NULL, SPS_IOVEC_FLAG_INT);

		if (ret) {
			pr_err("Failed to submit data desc %d\n", ret);
			goto put_dev;
		}
		/* Submit command descriptor */
		ret =  sps_transfer(info->sps.cmd_pipe.handle, &dma_buffer->xfer);
		if (ret) {
			pr_err("Failed to submit commands %d\n", ret);
			goto put_dev;
		}

		ret = msm_nand_sps_get_iovec(info, dma_buffer->xfer.iovec_count,
						submitted_num_desc, 0, 0, &iovec_temp);
		if (ret)
			goto put_dev;

		ret = msm_nand_put_device(chip->dev);
		mutex_unlock(&info->lock);
		if (ret)
			goto free_dma;

		/* Check for flash status errors */
		if (dma_buffer->flash_status & (FS_OP_ERR | FS_MPU_ERR)) {
			pr_err("MPU/OP err set: %x for page:%d\n",
					dma_buffer->flash_status, page);
			bad_page_count++;
			goto next_page;
		}

		/* Check for bad block marker byte */
		if (chip->cfg1 & (1 << WIDE_FLASH)) {
			if (buf[0] != 0xFF || buf[1] != 0xFF)
				bad_block = 1;
		} else {
			if (buf[0] != 0xFF)
				bad_block = 1;
		}
next_page:
		/* Check for 2nd page for NON-ONFI and last page for ONFI */
		if (!flash->is_onfi_compliant)
			page += 1;
		else
			page += pages_per_block - 1;

	} while ((++bad_block_page < BAD_BLOCK_CHECK_PAGES) && !bad_block);

	if (bad_page_count == BAD_BLOCK_CHECK_PAGES)
		bad_block = -EIO;

	goto free_dma;
put_dev:
	msm_nand_put_device(chip->dev);
	mutex_unlock(&info->lock);
free_dma:
	msm_nand_release_dma_buffer(chip, dma_buffer, sizeof(*dma_buffer) + 4);
out:
	return ret ? ret : bad_block;
}

/*
 * Function that gets called from upper layers such as MTD/YAFFS2 to mark a
 * block as bad. This is done by writing the first and second page
 * within a block for a NON_ONFI device  with 0 and first and last page for
 * an ONFI device, thus setting the bad block byte location as well to 0.
 */
static int msm_nand_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	struct msm_nand_info *info = mtd->priv;
	struct flash_identification *flash = &info->flash_dev;
	struct mtd_oob_ops ops;
	int ret, mark_block_bad_page = 0;
	uint8_t *buf;
	size_t len;
	uint32_t pages_per_block = 0;

	if (ofs > mtd->size) {
		pr_err("Invalid offset 0x%llx\n", ofs);
		ret = -EINVAL;
		goto out;
	}
	if (ofs & (mtd->erasesize - 1)) {
		pr_err("unsupported block address, 0x%x\n", (uint32_t)ofs);
		ret = -EINVAL;
		goto out;
	}
	len = mtd->writesize + mtd->oobsize;
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}
	if (mtd->writesize == PAGE_SIZE_2K)
		pages_per_block = mtd->erasesize >> 11;

	if (mtd->writesize == PAGE_SIZE_4K)
		pages_per_block = mtd->erasesize >> 12;
	do {
		ops.mode = MTD_OPS_RAW;
		ops.len = len;
		ops.retlen = 0;
		ops.ooblen = 0;
		ops.datbuf = buf;
		ops.oobbuf = NULL;
		ret =  msm_nand_write_oob(mtd, ofs, &ops);
		if (ret) {
			pr_err("write failed with err:%d for ofs:0x%x\n",
						ret, (uint32_t)ofs);
			goto free_mem;
		}
		/* Mark 2nd page for NON-ONFI with zeroes else last page with zeros */
		if (!flash->is_onfi_compliant)
			ofs += (loff_t)mtd->writesize;
		else
			ofs += (loff_t)((pages_per_block - 1) * mtd->writesize);
	} while (++mark_block_bad_page < BAD_BLOCK_CHECK_PAGES);

free_mem:
	kfree(buf);
out:
	return ret;
}

/*
 * Function that scans for the attached NAND device. This fills out all
 * the uninitialized function pointers with the defaults. The flash ID is
 * read and the mtd/chip structures are filled with the appropriate values.
 */
static int msm_nand_scan(struct mtd_info *mtd)
{
	struct msm_nand_info *info = mtd->priv;
	struct msm_nand_chip *chip = &info->nand_chip;
	struct flash_identification *supported_flash = &info->flash_dev;
	int err = 0;
	uint32_t i, j, mtd_writesize = 0;
	uint8_t dev_found = 0, wide_bus = 0;
	uint32_t manid, devid, devcfg;
	uint32_t flash_id = 0, flash_id2 = 0;
	uint8_t id_byte[NAND_MAX_ID_LEN];
	uint32_t bad_block_byte, spare_bytes;
	struct nand_flash_dev *flashdev = NULL;
	const struct nand_manufacturer_desc *flashman = NULL;

	/* Probe the Flash device for ONFI compliance */
	if (!msm_nand_flash_onfi_probe(info)) {
		dev_found = 1;
	} else {
		err = msm_nand_flash_read_id(info, 0, &flash_id, &flash_id2);
		if (err < 0) {
			pr_err("Failed to read Flash ID\n");
			err = -EINVAL;
			goto out;
		}
		manid  = id_byte[0] = flash_id & 0xFF;
		devid  = id_byte[1] = (flash_id >> 8) & 0xFF;
		devcfg = id_byte[3] = (flash_id >> 24) & 0xFF;
		id_byte[2] = (flash_id >> 16) & 0xFF;
		id_byte[4] = flash_id2 & 0xFF;
		id_byte[5] = (flash_id2 >> 8) & 0xFF;
		id_byte[6] = (flash_id2 >> 16) & 0xFF;
		id_byte[7] = (flash_id2 >> 24) & 0xFF;

		flashman = nand_get_manufacturer_desc(manid);

		for (i = 0; !flashdev; ++i) {
			/*
			 * If id_len is specified for an entry in the nand ids
			 * array, then at least 4 bytes of the nand id is
			 * present in the nand ids array - use that to identify
			 * the nand device first. If that is not present, only
			 * then fall back to searching the legacy or extended
			 * ids in the nand ids array.
			 * The id_len number of bytes in the nand id read from
			 * the device are checked against those in the nand id
			 * table for exact match.
			 */
			if (nand_flash_ids[i].id_len) {
				for (j = 0; j < nand_flash_ids[i].id_len; j++) {
					if (nand_flash_ids[i].id[j] ==
							id_byte[j])
						continue;
					else
						break;
				}
				if (j == nand_flash_ids[i].id_len)
					flashdev = &nand_flash_ids[i];
			} else if (!nand_flash_ids[i].id_len &&
					nand_flash_ids[i].dev_id == devid)
				flashdev = &nand_flash_ids[i];
		}
		if (!flashdev || !flashman) {
			pr_err("unknown nand flashid=%x manuf=%x devid=%x\n",
				flash_id, manid, devid);
			err = -ENOENT;
			goto out;
		}
		dev_found = 1;
		if (!flashdev->pagesize) {
			pr_err("missing page size info - extract from NAND ID\n");
			supported_flash->widebus = devcfg & (1 << 6) ? 1 : 0;
			supported_flash->pagesize = 1024 << (devcfg & 0x3);
			supported_flash->blksize = (64 * 1024) <<
							((devcfg >> 4) & 0x3);
			supported_flash->oobsize = (8 << ((devcfg >> 2) & 1)) *
				(supported_flash->pagesize >> 9);
		} else {
			supported_flash->widebus = flashdev->options &
				       NAND_BUSWIDTH_16 ? 1 : 0;
			supported_flash->pagesize = flashdev->pagesize;
			supported_flash->blksize = flashdev->erasesize;
			supported_flash->oobsize = flashdev->oobsize;
			supported_flash->ecc_correctability =
					flashdev->ecc.strength_ds;
			if (!flashdev->ecc.strength_ds)
				pr_err("num ecc correctable bit not specified and defaults to 4 bit BCH\n");
		}
		supported_flash->flash_id = flash_id;
		supported_flash->density = ((uint64_t)flashdev->chipsize) << 20;
	}

	if (dev_found) {
		wide_bus       = supported_flash->widebus;
		mtd->size      = supported_flash->density;
		mtd->writesize = supported_flash->pagesize;
		mtd->oobsize   = supported_flash->oobsize;
		mtd->erasesize = supported_flash->blksize;
		mtd->writebufsize = mtd->writesize;
		mtd_writesize = mtd->writesize;

		/* Check whether NAND device support 8bit ECC*/
		if (supported_flash->ecc_correctability >= 8) {
			chip->bch_caps = MSM_NAND_CAP_8_BIT_BCH;
			supported_flash->ecc_capability = 8;
		} else {
			chip->bch_caps = MSM_NAND_CAP_4_BIT_BCH;
			supported_flash->ecc_capability = 4;
		}

		pr_info("NAND Id: 0x%x Buswidth: %dBits Density: %lld MByte\n",
			supported_flash->flash_id, (wide_bus) ? 16 : 8,
			(mtd->size >> 20));
		pr_info("pagesize: %d Erasesize: %d oobsize: %d (in Bytes)\n",
			mtd->writesize, mtd->erasesize, mtd->oobsize);
		pr_info("BCH ECC: %d Bit\n", supported_flash->ecc_capability);
	}

	chip->cw_size = (chip->bch_caps & MSM_NAND_CAP_8_BIT_BCH) ? 532 : 528;
	chip->cfg0 = (((mtd_writesize >> 9) - 1) << CW_PER_PAGE)
		|  (516 <<  UD_SIZE_BYTES)
		|  (0 << DISABLE_STATUS_AFTER_WRITE)
		|  (5 << NUM_ADDR_CYCLES);

	bad_block_byte = (mtd_writesize - (chip->cw_size * (
					(mtd_writesize >> 9) - 1)) + 1);
	chip->cfg1 = (7 <<  NAND_RECOVERY_CYCLES)
		|    (0 <<  CS_ACTIVE_BSY)
		|    (bad_block_byte <<  BAD_BLOCK_BYTE_NUM)
		|    (0 << BAD_BLOCK_IN_SPARE_AREA)
		|    (2 << WR_RD_BSY_GAP)
		| ((wide_bus ? 1 : 0) << WIDE_FLASH)
		| (1 << ENABLE_BCH_ECC);

	/*
	 * For 4bit BCH ECC (default ECC), parity bytes = 7(x8) or 8(x16 I/O)
	 * For 8bit BCH ECC, parity bytes = 13 (x8) or 14 (x16 I/O).
	 */
	chip->ecc_parity_bytes = (chip->bch_caps & MSM_NAND_CAP_8_BIT_BCH) ?
				(wide_bus ? 14 : 13) : (wide_bus ? 8 : 7);

	spare_bytes = chip->cw_size - (BYTES_512 + chip->ecc_parity_bytes);
	chip->cfg0_raw = (((mtd_writesize >> 9) - 1) << CW_PER_PAGE)
		|	(5 << NUM_ADDR_CYCLES)
		|	(spare_bytes << SPARE_SIZE_BYTES)
		|	(BYTES_512 << UD_SIZE_BYTES);

	chip->cfg1_raw = (2 << WR_RD_BSY_GAP)
		|    (1 << BAD_BLOCK_IN_SPARE_AREA)
		|    (21 <<  BAD_BLOCK_BYTE_NUM)
		|    (0 <<  CS_ACTIVE_BSY)
		| (7 <<  NAND_RECOVERY_CYCLES)
		| ((wide_bus ? 1 : 0) << WIDE_FLASH)
		| (1 << DEV0_CFG1_ECC_DISABLE);

	chip->ecc_bch_cfg = (0 << ECC_CFG_ECC_DISABLE)
			|   (0 << ECC_SW_RESET)
			|   (516 << ECC_NUM_DATA_BYTES)
			|   (chip->ecc_parity_bytes << ECC_PARITY_SIZE_BYTES)
			|   (1 << ECC_FORCE_CLK_OPEN);

	chip->ecc_cfg_raw = (1 << ECC_FORCE_CLK_OPEN)
			|   (BYTES_512 << ECC_NUM_DATA_BYTES)
			|   (chip->ecc_parity_bytes << ECC_PARITY_SIZE_BYTES)
			|   (0 << ECC_SW_RESET)
			|   (1 << ECC_CFG_ECC_DISABLE);

	if (chip->bch_caps & MSM_NAND_CAP_8_BIT_BCH) {
		chip->cfg0 |= (wide_bus ? 0 << SPARE_SIZE_BYTES :
				2 << SPARE_SIZE_BYTES);
		chip->ecc_bch_cfg |= (1 << ECC_MODE);
		chip->ecc_cfg_raw |= (1 << ECC_MODE);
	} else {
		chip->cfg0 |= (wide_bus ? 2 << SPARE_SIZE_BYTES :
				4 << SPARE_SIZE_BYTES);
		chip->ecc_bch_cfg |= (0 << ECC_MODE);
		chip->ecc_cfg_raw |= (0 << ECC_MODE);
	}

	chip->ecc_buf_cfg = 0x203; /* No of bytes covered by ECC - 516 bytes */

	pr_info("CFG0: 0x%08x,           CFG1: 0x%08x\n"
		"            RAWCFG0: 0x%08x,        RAWCFG1: 0x%08x\n"
		"          ECCBUFCFG: 0x%08x,      ECCBCHCFG: 0x%08x\n"
		"          RAWECCCFG: 0x%08x, BAD BLOCK BYTE: 0x%08x\n",
		chip->cfg0, chip->cfg1,	chip->cfg0_raw, chip->cfg1_raw,
		chip->ecc_buf_cfg, chip->ecc_bch_cfg,
		chip->ecc_cfg_raw, bad_block_byte);

	if (mtd->writesize == 2048)
		mtd->oobavail = 16;
	else if (mtd->writesize == 4096)
		mtd->oobavail = 32;
	else {
		pr_err("Unsupported NAND pagesize: 0x%x\n", mtd->writesize);
		err = -ENODEV;
		goto out;
	}

	/* Fill in remaining MTD driver data */
	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH;
	mtd->_erase = msm_nand_erase;
	mtd->_block_isbad = msm_nand_block_isbad;
	mtd->_block_markbad = msm_nand_block_markbad;
	mtd->_read = msm_nand_read;
	mtd->_write = msm_nand_write;
	mtd->owner = THIS_MODULE;
out:
	return err;
}

#define BAM_APPS_PIPE_LOCK_GRP0 0
#define BAM_APPS_PIPE_LOCK_GRP1 1
/*
 * This function allocates, configures, connects an end point and
 * also registers event notification for an end point. It also allocates
 * DMA memory for descriptor FIFO of a pipe.
 */
static int msm_nand_init_endpoint(struct msm_nand_info *info,
				struct msm_nand_sps_endpt *end_point,
				uint32_t pipe_index)
{
	int rc = 0;
	struct sps_pipe *pipe_handle;
	struct sps_connect *sps_config = &end_point->config;
	struct sps_register_event *sps_event = &end_point->event;

	pipe_handle = sps_alloc_endpoint();
	if (!pipe_handle) {
		pr_err("sps_alloc_endpoint() failed\n");
		rc = -ENOMEM;
		goto out;
	}

	rc = sps_get_config(pipe_handle, sps_config);
	if (rc) {
		pr_err("sps_get_config() failed %d\n", rc);
		goto free_endpoint;
	}

	if (pipe_index == SPS_DATA_PROD_PIPE_INDEX ||
		pipe_index == SPS_DATA_PROD_STAT_PIPE_INDEX) {
		/* READ CASE: source - BAM; destination - system memory */
		sps_config->source = info->sps.bam_handle;
		sps_config->destination = SPS_DEV_HANDLE_MEM;
		sps_config->mode = SPS_MODE_SRC;
		sps_config->src_pipe_index = pipe_index;
	} else if (pipe_index == SPS_DATA_CONS_PIPE_INDEX ||
			pipe_index == SPS_CMD_CONS_PIPE_INDEX) {
		/* WRITE CASE: source - system memory; destination - BAM */
		sps_config->source = SPS_DEV_HANDLE_MEM;
		sps_config->destination = info->sps.bam_handle;
		sps_config->mode = SPS_MODE_DEST;
		sps_config->dest_pipe_index = pipe_index;
	}

	sps_config->options = SPS_O_AUTO_ENABLE | SPS_O_ACK_TRANSFERS;

	if (msm_nand_is_interrupt_mode_enable(info)) {
		switch (pipe_index) {
		case SPS_DATA_PROD_PIPE_INDEX:
		case SPS_DATA_PROD_STAT_PIPE_INDEX:
		case SPS_DATA_CONS_PIPE_INDEX:
			sps_config->options |= SPS_O_HYBRID | SPS_O_EOT;
			break;
		case SPS_CMD_CONS_PIPE_INDEX:
			sps_config->options |= SPS_O_HYBRID | SPS_O_DESC_DONE;
		}
	} else
		sps_config->options |= SPS_O_POLL;

	if (pipe_index == SPS_DATA_PROD_PIPE_INDEX ||
			pipe_index == SPS_DATA_CONS_PIPE_INDEX ||
			pipe_index == SPS_DATA_PROD_STAT_PIPE_INDEX)
		sps_config->lock_group = BAM_APPS_PIPE_LOCK_GRP0;
	else if (pipe_index == SPS_CMD_CONS_PIPE_INDEX)
		sps_config->lock_group = BAM_APPS_PIPE_LOCK_GRP1;

	/*
	 * Descriptor FIFO is a cyclic FIFO. If SPS_MAX_DESC_NUM descriptors
	 * are allowed to be submitted before we get any ack for any of them,
	 * the descriptor FIFO size should be: (SPS_MAX_DESC_NUM + 1) *
	 * sizeof(struct sps_iovec).
	 */
	sps_config->desc.size = (SPS_MAX_DESC_NUM + 1) *
					sizeof(struct sps_iovec);
	sps_config->desc.base = dmam_alloc_coherent(info->nand_chip.dev,
					sps_config->desc.size,
					&sps_config->desc.phys_base,
					GFP_KERNEL);
	if (!sps_config->desc.base) {
		pr_err("dmam_alloc_coherent() failed for size %x\n",
				sps_config->desc.size);
		rc = -ENOMEM;
		goto free_endpoint;
	}
	memset(sps_config->desc.base, 0x00, sps_config->desc.size);

	rc = sps_connect(pipe_handle, sps_config);
	if (rc) {
		pr_err("sps_connect() failed %d\n", rc);
		goto free_endpoint;
	}

	sps_event->options = SPS_O_EOT;

	if (msm_nand_is_interrupt_mode_enable(info)) {
		if (pipe_index == SPS_CMD_CONS_PIPE_INDEX)
			sps_event->options = SPS_O_DESC_DONE;
		init_completion(&(end_point->completion));
		sps_event->xfer_done = &(end_point->completion);
	}

	sps_event->mode = SPS_TRIGGER_WAIT;
	sps_event->user = (void *)info;

	rc = sps_register_event(pipe_handle, sps_event);
	if (rc) {
		pr_err("sps_register_event() failed %d\n", rc);
		goto sps_disconnect;
	}
	end_point->index = pipe_index;
	end_point->handle = pipe_handle;
	pr_debug("pipe handle 0x%lx for pipe %d\n", (unsigned long)pipe_handle,
			pipe_index);
	goto out;
sps_disconnect:
	sps_disconnect(pipe_handle);
free_endpoint:
	sps_free_endpoint(pipe_handle);
out:
	return rc;
}

/* This function disconnects and frees an end point */
static void msm_nand_deinit_endpoint(struct msm_nand_info *info,
				struct msm_nand_sps_endpt *end_point)
{
	sps_disconnect(end_point->handle);
	sps_free_endpoint(end_point->handle);
}

/*
 * This function registers BAM device and initializes its end points for
 * the following pipes -
 * system consumer pipe for data (pipe#0),
 * system producer pipe for data (pipe#1),
 * system consumer pipe for commands (pipe#2).
 */
static int msm_nand_bam_init(struct msm_nand_info *nand_info)
{
	struct sps_bam_props bam = {0};
	int rc = 0;
	struct msm_nand_chip *chip = &nand_info->nand_chip;

	bam.phys_addr = nand_info->bam_phys;
	bam.virt_addr = nand_info->bam_base;
	bam.irq = nand_info->bam_irq;
	/*
	 * NAND device is accessible from both Apps and Modem processor and
	 * thus, NANDc and BAM are shared between both the processors. But BAM
	 * must be enabled and instantiated only once during boot up by
	 * Trustzone before Modem/Apps is brought out from reset.
	 *
	 * This is indicated to SPS driver on Apps by marking flag
	 * SPS_BAM_MGR_DEVICE_REMOTE. The following are the global
	 * initializations that will be done by Trustzone - Execution
	 * Environment, Pipes assignment to Apps/Modem, Pipe Super groups and
	 * Descriptor summing threshold.
	 *
	 * NANDc BAM device supports 2 execution environments - Modem and Apps
	 * and thus the flag SPS_BAM_MGR_MULTI_EE is set.
	 */
	bam.manage = SPS_BAM_MGR_DEVICE_REMOTE | SPS_BAM_MGR_MULTI_EE;

	bam.ipc_loglevel = QPIC_BAM_DEFAULT_IPC_LOGLVL;
	mutex_lock(&nand_info->lock);
	rc = msm_nand_get_device(chip->dev);
	if (rc) {
		pr_err("failed to get the device err:%d\n", rc);
		goto out;
	}
	rc = sps_phy2h(bam.phys_addr, &nand_info->sps.bam_handle);
	if (!rc)
		goto init_sps_ep;
	rc = sps_register_bam_device(&bam, &nand_info->sps.bam_handle);
	if (rc) {
		pr_err("%s: sps_register_bam_device() failed with %d\n",
			__func__, rc);
		goto put_dev;
	}
	pr_info("%s: BAM device registered: bam_handle 0x%lx\n",
			__func__, nand_info->sps.bam_handle);
init_sps_ep:
	rc = msm_nand_init_endpoint(nand_info, &nand_info->sps.data_prod,
					SPS_DATA_PROD_PIPE_INDEX);
	if (rc)
		goto put_dev;
	rc = msm_nand_init_endpoint(nand_info, &nand_info->sps.data_cons,
					SPS_DATA_CONS_PIPE_INDEX);
	if (rc)
		goto deinit_data_prod;

	rc = msm_nand_init_endpoint(nand_info, &nand_info->sps.cmd_pipe,
					SPS_CMD_CONS_PIPE_INDEX);
	if (rc)
		goto deinit_data_cons;
	goto put_dev;
deinit_data_cons:
	msm_nand_deinit_endpoint(nand_info, &nand_info->sps.data_cons);
deinit_data_prod:
	msm_nand_deinit_endpoint(nand_info, &nand_info->sps.data_prod);
put_dev:
	rc = msm_nand_put_device(chip->dev);
out:
	mutex_unlock(&nand_info->lock);
	return rc;
}

/*
 * This function disconnects and frees its end points for all the pipes.
 * Since the BAM is shared resource, it is not deregistered as its handle
 * might be in use with LCDC.
 */
static void msm_nand_bam_free(struct msm_nand_info *nand_info)
{
	msm_nand_deinit_endpoint(nand_info, &nand_info->sps.data_prod);
	msm_nand_deinit_endpoint(nand_info, &nand_info->sps.data_cons);
	msm_nand_deinit_endpoint(nand_info, &nand_info->sps.cmd_pipe);
	if (nand_info->nand_chip.qpic_version >= 2)
		msm_nand_deinit_endpoint(nand_info,
			&nand_info->sps.data_prod_stat);
}

/* This function enables DMA support for the NANDc in BAM mode. */
static int msm_nand_enable_dma(struct msm_nand_info *info)
{
	uint32_t reg = 0;
	int err;

	err = msm_nand_flash_rd_rw_reg(info, MSM_NAND_CTRL(info), &reg, READ);
	if (!err) {
		if (FIELD_GET(BAM_MODE_EN_MASK, reg) == 0x1)
			return 0;
		reg |= FIELD_PREP(BAM_MODE_EN_MASK, 0x1);
		err = msm_nand_flash_rd_rw_reg(info, MSM_NAND_CTRL(info),
			&reg, WRITE);
	}
	return err;
}

/* Enable the boost mode based on flags */
static int msm_nand_boost_mode_enable(struct msm_nand_info *info)
{
	int err = 0;
	uint32_t reg = 0;

	if ((info->nand_chip.caps & MSM_NAND_CAP_BOOST_MODE) &&
		(info->flash_dev.timing_mode_support == 0x1F)) {
		err = msm_nand_flash_rd_rw_reg(info, MSM_NAND_CTRL(info),
				&reg, READ);
		if (!err) {
			reg |= (1 << BOOST_MODE_EN);
			err = msm_nand_flash_rd_rw_reg(info, MSM_NAND_CTRL(info),
					&reg, WRITE);
		}
	}

	return err;
}

static int msm_nand_parse_smem_ptable(int *nr_parts)
{

	uint32_t  i, j;
	size_t len = FLASH_PTABLE_HDR_LEN;
	struct flash_partition_entry *pentry;
	char *delimiter = ":";
	void *temp_ptable = NULL;
	char *name = NULL;

	pr_info("Parsing partition table info from SMEM\n");
	temp_ptable = qcom_smem_get(SMEM_APPS, SMEM_AARM_PARTITION_TABLE, &len);


	if (IS_ERR_OR_NULL(temp_ptable)) {
		pr_err("Error reading partition table header\n");
		goto out;
	}

	/* Read only the header portion of ptable */
	ptable = *(struct flash_partition_table *)temp_ptable;

	/* Verify ptable magic */
	if (ptable.magic1 != FLASH_PART_MAGIC1 ||
			ptable.magic2 != FLASH_PART_MAGIC2) {
		pr_err("Partition table magic verification failed\n");
		goto out;
	}
	/* Ensure that # of partitions is less than the max we have allocated */
	if (ptable.numparts > FLASH_PTABLE_MAX_PARTS_V4) {
		pr_err("Partition numbers exceed the max limit\n");
		goto out;
	}
	/* Find out length of partition data based on table version. */
	if (ptable.version <= FLASH_PTABLE_V3) {
		len = FLASH_PTABLE_HDR_LEN + FLASH_PTABLE_MAX_PARTS_V3 *
			sizeof(struct flash_partition_entry);
	} else if (ptable.version == FLASH_PTABLE_V4) {
		len = FLASH_PTABLE_HDR_LEN + FLASH_PTABLE_MAX_PARTS_V4 *
			sizeof(struct flash_partition_entry);
	} else {
		pr_err("Unknown ptable version (%d)\n", ptable.version);
		goto out;
	}

	*nr_parts = ptable.numparts;

	/*
	 * Now that the partition table header has been parsed, verified
	 * and the length of the partition table calculated, read the
	 * complete partition table.
	 */
	temp_ptable = qcom_smem_get(SMEM_APPS, SMEM_AARM_PARTITION_TABLE, &len);
	if (IS_ERR_OR_NULL(temp_ptable)) {
		pr_err("Error reading partition table\n");
		goto out;
	}

	/* Read only the header portion of ptable */
	ptable = *(struct flash_partition_table *)temp_ptable;

	for (i = 0; i < ptable.numparts; i++) {
		pentry = &ptable.part_entry[i];
		if (pentry->name[0] == '\0')
			continue;
		/* Convert name to lower case and discard the initial chars */
		name = pentry->name;
		strsep(&name, delimiter);
		mtd_part[i].name = name;
		if (!mtd_part[i].name)
			mtd_part[i].name = pentry->name;
		for (j = 0; j < strlen(mtd_part[i].name); j++)
			*((char *)mtd_part[i].name + j) =
				tolower(*((char *)mtd_part[i].name + j));
		mtd_part[i].offset      = pentry->offset;
		mtd_part[i].mask_flags  = pentry->attr;
		mtd_part[i].size        = pentry->length;
		pr_debug("%d: %s offs=0x%08x size=0x%08x attr:0x%08x\n",
			i, pentry->name, pentry->offset, pentry->length,
			pentry->attr);
	}
	pr_info("SMEM partition table found: ver: %d len: %d\n",
		ptable.version, ptable.numparts);
	return 0;
out:
	return -EINVAL;
}

/*
 * This function allocates, configures, connects Staus pipe end point (Pipe#3)
 * and also registers event notification for that end point. It also
 * allocates DMA memory for descriptor FIFO of that pipe.
 */
static int msm_nand_init_status_pipe(struct msm_nand_info *info)
{
	int err;

	mutex_lock(&info->lock);
	err = msm_nand_get_device(info->nand_chip.dev);
	if (err) {
		pr_err("Failed to get the device err=%d\n", err);
		goto out;
	}
	err = msm_nand_init_endpoint(info,
		&info->sps.data_prod_stat,
		SPS_DATA_PROD_STAT_PIPE_INDEX);
	if (err)
		pr_err("Failed to configure read status pipe err=%d\n",
			err);

	err = msm_nand_put_device(info->nand_chip.dev);

out:
	mutex_unlock(&info->lock);
	return err;
}

static int msm_nand_bam_panic_notifier(struct notifier_block *this,
					unsigned long event, void *ptr)
{
	struct msm_nand_info *info = dev_get_drvdata(dev_node);
	struct msm_nand_chip *chip = &info->nand_chip;
	int err = 0;

	/* We shouldn't request for a new resource during panic
	 * as the cores and irq's were already in disabled state.
	 * So, check device runtime status before request for a
	 * resource (clock and bus).
	 */

	if (pm_runtime_suspended(chip->dev))
		return NOTIFY_DONE;

	/*
	 * It is sufficient to dump the BAM debug dumps on the
	 * very first crash and ignore any subsequent/back-to-back
	 * crashes.
	 */
	if (!test_and_set_bit(0, &info->panic_notifier_dump)) {
		err = msm_nand_get_device(chip->dev);
		if (err)
			goto out;
		pr_debug("Dumping APSS bam pipes register dumps\n");
		sps_get_bam_debug_info(info->sps.bam_handle, 93,
				(SPS_BAM_PIPE(0) |
				SPS_BAM_PIPE(1) |
				SPS_BAM_PIPE(2) |
				SPS_BAM_PIPE(3)),
				0, 2);
		err = msm_nand_put_device(chip->dev);
	}
out:
	if (err)
		pr_err("Failed to get/put the device. err:%d\n", err);
	return NOTIFY_DONE;
}

static struct notifier_block msm_nand_bam_panic_blk = {
	.notifier_call = msm_nand_bam_panic_notifier,
};

void msm_nand_bam_register_panic_handler(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
			&msm_nand_bam_panic_blk);
}

void msm_nand_bam_unregister_panic_handler(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list,
					&msm_nand_bam_panic_blk);
}

#define BOOT_DEV_MASK 0x1E
#define BOOT_DEV_NAND 0x4

/*
 * This function gets called when its device named msm-nand is added to
 * device tree .dts file with all its resources such as physical addresses
 * for NANDc and BAM, BAM IRQ.
 *
 * It also expects the NAND flash partition information to be passed in .dts
 * file so that it can parse the partitions by calling MTD function
 * mtd_device_parse_register().
 *
 */
static int msm_nand_probe(struct platform_device *pdev)
{
	struct msm_nand_info *info;
	struct resource *res;
	int i, err, nr_parts;
	struct device *dev;
	u32 adjustment_offset;
	struct version qpic_version = {0};

	msm_nand_boot_device_is_nand(pdev);
	/* If boot device is not NAND return success
	 * from NAND probe.
	 */
	if (!is_bootdevice_nand)
		return 0;
	/*
	 * The partition information can also be passed from kernel command
	 * line. Also, the MTD core layer supports adding the whole device as
	 * one MTD device when no partition information is available at all.
	 */
	info = devm_kzalloc(&pdev->dev, sizeof(struct msm_nand_info),
				GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto out;
	}
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"nand_phys");
	if (!res || !res->start) {
		pr_err("NAND phys address range is not provided\n");
		err = -ENODEV;
		goto out;
	}
	info->nand_phys = res->start;

	if (of_property_read_bool(pdev->dev.of_node, "qcom,boost_mode"))
		info->nand_chip.caps |= MSM_NAND_CAP_BOOST_MODE;

	/* Enable Interrupt mode based on flags
	 * Update bam_irq_type.
	 */
	if (of_property_read_bool(pdev->dev.of_node,
				"qcom,nand_interrupt_mode")) {
		info->nand_chip.caps |= MSM_NAND_INTERRUPT_MODE_ENABLE;
		of_property_read_u32_index(pdev->dev.of_node, "interrupts", 2,
						&(info->bam_irq_type));
		if (info->bam_irq_type == IRQ_TYPE_EDGE_RISING) {
			pr_err("Edge trigger interrupt mode not supported\n");
			err = -EPERM;
			goto out;
		}
	}

	err = of_property_read_u32(pdev->dev.of_node,
				   "qcom,reg-adjustment-offset",
				   &adjustment_offset);
	if (err) {
		pr_err("adjustment_offset not found, err = %d\n", err);
		WARN_ON(1);
		return err;
	}

	info->nand_phys_adjusted = info->nand_phys + adjustment_offset;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"bam_phys");
	if (!res || !res->start) {
		pr_err("BAM phys address range is not provided\n");
		err = -ENODEV;
		goto out;
	}
	info->bam_phys = res->start;
	info->bam_base = devm_ioremap(&pdev->dev, res->start,
					resource_size(res));
	if (!info->bam_base) {
		resource_size_t size = resource_size(res);

		pr_err("BAM ioremap() failed for addr %pap size %pap\n",
			&res->start, &size);
		err = -ENOMEM;
		goto out;
	}

	info->bam_irq = platform_get_irq_byname(pdev, "bam_irq");
	if (info->bam_irq < 0) {
		pr_err("BAM IRQ is not provided\n");
		err = -ENODEV;
		goto out;
	}

	info->mtd.name = dev_name(&pdev->dev);
	info->mtd.priv = info;
	info->mtd.owner = THIS_MODULE;
	info->nand_chip.dev = &pdev->dev;
	init_waitqueue_head(&info->nand_chip.dma_wait_queue);
	mutex_init(&info->lock);

	dev = &pdev->dev;
	if (!dma_set_coherent_mask(dev, DMA_BIT_MASK(32)))
		info->dma_mask = DMA_BIT_MASK(32);

	info->nand_chip.dma_virt_addr =
		dmam_alloc_coherent(&pdev->dev, MSM_NAND_DMA_BUFFER_SIZE,
			&info->nand_chip.dma_phys_addr, GFP_KERNEL);
	if (!info->nand_chip.dma_virt_addr) {
		pr_err("No memory for DMA buffer size %x\n",
				MSM_NAND_DMA_BUFFER_SIZE);
		err = -ENOMEM;
		goto out;
	}
	err = msm_nand_bus_register(pdev, info);
	if (err)
		goto out;

	if (of_property_read_bool(pdev->dev.of_node, "qcom,qpic-clk-rpmh"))
		info->clk_data.rpmh_clk = true;

	if (!info->clk_data.rpmh_clk) {
		info->clk_data.qpic_clk = devm_clk_get(&pdev->dev, "core_clk");
		if (!IS_ERR_OR_NULL(info->clk_data.qpic_clk)) {
			err = clk_set_rate(info->clk_data.qpic_clk,
				MSM_NAND_BUS_VOTE_MAX_RATE);
		} else {
			err = PTR_ERR(info->clk_data.qpic_clk);
			pr_err("Failed to get clock handle, err=%d\n", err);
		}
		if (err)
			goto bus_unregister;
	}

	err = msm_nand_setup_clocks_and_bus_bw(info, true);
	if (err)
		goto bus_unregister;
	dev_set_drvdata(&pdev->dev, info);
	err = pm_runtime_set_active(&pdev->dev);
	if (err)
		pr_err("pm_runtime_set_active() failed with error %d\n", err);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, MSM_NAND_IDLE_TIMEOUT);

	err = msm_nand_bam_init(info);
	if (err) {
		pr_err("msm_nand_bam_init() failed %d\n", err);
		goto clk_rpm_disable;
	}
	err = msm_nand_enable_dma(info);
	if (err) {
		pr_err("Failed to enable DMA in NANDc\n");
		goto free_bam;
	}
	err = msm_nand_version_check(info, &qpic_version);
	if (err) {
		pr_err("Failed to read the version information\n");
		goto free_bam;
	}
	info->nand_chip.qpic_version = qpic_version.qpic_major;
	info->nand_chip.qpic_min_version = qpic_version.qpic_minor;
	if (info->nand_chip.qpic_version >= 2 &&
			info->nand_chip.qpic_min_version >= 1) {
		info->nand_chip.caps |= MSM_NAND_CAP_PAGE_SCOPE_READ;
		err = msm_nand_init_status_pipe(info);
		if (err)
			goto free_bam;
	}
	err = msm_nand_parse_smem_ptable(&nr_parts);
	if (err < 0) {
		pr_err("Failed to parse partition table in SMEM\n");
		goto free_bam;
	}
	if (msm_nand_scan(&info->mtd)) {
		pr_err("No nand device found\n");
		err = -ENXIO;
		goto free_bam;
	}

	err = msm_nand_boost_mode_enable(info);
	if (unlikely(err))
		pr_err("Failed to enable Boost Mode Err: %d\n", err);

	for (i = 0; i < nr_parts; i++) {
		mtd_part[i].offset *= info->mtd.erasesize;
		mtd_part[i].size *= info->mtd.erasesize;
	}
	err = mtd_device_parse_register(&info->mtd, NULL, NULL,
		&mtd_part[0], nr_parts);
	if (err < 0) {
		pr_err("Unable to register MTD partitions %d\n", err);
		goto free_bam;
	}

	pr_info("NANDc phys addr 0x%lx, BAM phys addr 0x%lx, BAM IRQ %d\n",
			info->nand_phys, info->bam_phys, info->bam_irq);
	pr_info("Allocated DMA buffer at virt_addr 0x%pK, phys_addr %pad\n",
		info->nand_chip.dma_virt_addr, &info->nand_chip.dma_phys_addr);
	pr_info("Host capabilities:0x%08x\n", info->nand_chip.caps);
	dev_node = dev;
	msm_nand_bam_register_panic_handler();
	goto out;
free_bam:
	msm_nand_bam_free(info);
clk_rpm_disable:
	msm_nand_setup_clocks_and_bus_bw(info, false);
	pm_runtime_disable(&(pdev)->dev);
	pm_runtime_set_suspended(&(pdev)->dev);
bus_unregister:
	msm_nand_bus_unregister(info);
out:
	return err;
}

/*
 * Remove functionality that gets called when driver/device msm-nand
 * is removed.
 */
static int msm_nand_remove(struct platform_device *pdev)
{

	struct msm_nand_info *info;

	/* If boot device is not NAND return success */
	if (!is_bootdevice_nand)
		return 0;
	info = dev_get_drvdata(&pdev->dev);
	msm_nand_bam_unregister_panic_handler();
	if (pm_runtime_suspended(&(pdev)->dev))
		pm_runtime_resume(&(pdev)->dev);

	dev_set_drvdata(&pdev->dev, NULL);

	if (info) {
		mtd_device_unregister(&info->mtd);
		msm_nand_bam_free(info);
		msm_nand_setup_clocks_and_bus_bw(info, false);
		msm_nand_bus_unregister(info);
	}

	pm_runtime_disable(&(pdev)->dev);
	pm_runtime_set_suspended(&(pdev)->dev);

	return 0;
}

static void msm_nand_shutdown(struct platform_device *pdev)
{
	struct msm_nand_info *info;

	/* If boot device is not NAND return success */
	if (!is_bootdevice_nand)
		return;
	info = dev_get_drvdata(&pdev->dev);
	mutex_lock(&info->lock);
	pr_debug("reboot handler\n");

	pm_runtime_disable(&(pdev)->dev);
	pm_runtime_set_suspended(&(pdev)->dev);

	mutex_unlock(&info->lock);
}

#define DRIVER_NAME "msm_qpic_nand"
static const struct of_device_id msm_nand_match_table[] = {
	{ .compatible = "qcom,msm-nand", },
	{},
};

static const struct dev_pm_ops msm_nand_pm_ops = {
	.suspend		= msm_nand_suspend,
	.resume			= msm_nand_resume,
	.runtime_suspend	= msm_nand_runtime_suspend,
	.runtime_resume		= msm_nand_runtime_resume,
};

static struct platform_driver msm_nand_driver = {
	.probe		= msm_nand_probe,
	.remove		= msm_nand_remove,
	.shutdown	= msm_nand_shutdown,
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table = msm_nand_match_table,
		.pm		= &msm_nand_pm_ops,
	},
};

module_platform_driver(msm_nand_driver);

MODULE_ALIAS(DRIVER_NAME);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MSM QPIC NAND flash driver");
