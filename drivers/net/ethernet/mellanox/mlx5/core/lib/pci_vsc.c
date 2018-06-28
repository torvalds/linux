// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies */

#include <linux/pci.h>
#include "mlx5_core.h"
#include "pci_vsc.h"

#define MLX5_EXTRACT_C(source, offset, size)	\
	((((u32)(source)) >> (offset)) & MLX5_ONES32(size))
#define MLX5_EXTRACT(src, start, len)		\
	(((len) == 32) ? (src) : MLX5_EXTRACT_C(src, start, len))
#define MLX5_ONES32(size)			\
	((size) ? (0xffffffff >> (32 - (size))) : 0)
#define MLX5_MASK32(offset, size)		\
	(MLX5_ONES32(size) << (offset))
#define MLX5_MERGE_C(rsrc1, rsrc2, start, len)  \
	((((rsrc2) << (start)) & (MLX5_MASK32((start), (len)))) | \
	((rsrc1) & (~MLX5_MASK32((start), (len)))))
#define MLX5_MERGE(rsrc1, rsrc2, start, len)	\
	(((len) == 32) ? (rsrc2) : MLX5_MERGE_C(rsrc1, rsrc2, start, len))
#define vsc_read(dev, offset, val) \
	pci_read_config_dword((dev)->pdev, (dev)->vsc_addr + (offset), (val))
#define vsc_write(dev, offset, val) \
	pci_write_config_dword((dev)->pdev, (dev)->vsc_addr + (offset), (val))
#define VSC_MAX_RETRIES 2048

enum mlx5_vsc_state {
	MLX5_VSC_UNLOCK,
	MLX5_VSC_LOCK,
};

enum {
	VSC_CTRL_OFFSET = 0x4,
	VSC_COUNTER_OFFSET = 0x8,
	VSC_SEMAPHORE_OFFSET = 0xc,
	VSC_ADDR_OFFSET = 0x10,
	VSC_DATA_OFFSET = 0x14,

	VSC_FLAG_BIT_OFFS = 31,
	VSC_FLAG_BIT_LEN = 1,

	VSC_SYND_BIT_OFFS = 30,
	VSC_SYND_BIT_LEN = 1,

	VSC_ADDR_BIT_OFFS = 0,
	VSC_ADDR_BIT_LEN = 30,

	VSC_SPACE_BIT_OFFS = 0,
	VSC_SPACE_BIT_LEN = 16,

	VSC_SIZE_VLD_BIT_OFFS = 28,
	VSC_SIZE_VLD_BIT_LEN = 1,

	VSC_STATUS_BIT_OFFS = 29,
	VSC_STATUS_BIT_LEN = 3,
};

void mlx5_pci_vsc_init(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_is_pf(dev))
		return;

	dev->vsc_addr = pci_find_capability(dev->pdev,
					    PCI_CAP_ID_VNDR);
	if (!dev->vsc_addr)
		mlx5_core_warn(dev, "Failed to get valid vendor specific ID\n");
}

int mlx5_vsc_gw_lock(struct mlx5_core_dev *dev)
{
	u32 counter = 0;
	int retries = 0;
	u32 lock_val;
	int ret;

	pci_cfg_access_lock(dev->pdev);
	do {
		if (retries > VSC_MAX_RETRIES) {
			ret = -EBUSY;
			goto pci_unlock;
		}

		/* Check if semaphore is already locked */
		ret = vsc_read(dev, VSC_SEMAPHORE_OFFSET, &lock_val);
		if (ret)
			goto pci_unlock;

		if (lock_val) {
			retries++;
			usleep_range(1000, 2000);
			continue;
		}

		/* Read and write counter value, if written value is
		 * the same, semaphore was acquired successfully.
		 */
		ret = vsc_read(dev, VSC_COUNTER_OFFSET, &counter);
		if (ret)
			goto pci_unlock;

		ret = vsc_write(dev, VSC_SEMAPHORE_OFFSET, counter);
		if (ret)
			goto pci_unlock;

		ret = vsc_read(dev, VSC_SEMAPHORE_OFFSET, &lock_val);
		if (ret)
			goto pci_unlock;

		retries++;
	} while (counter != lock_val);

	return 0;

pci_unlock:
	pci_cfg_access_unlock(dev->pdev);
	return ret;
}

int mlx5_vsc_gw_unlock(struct mlx5_core_dev *dev)
{
	int ret;

	ret = vsc_write(dev, VSC_SEMAPHORE_OFFSET, MLX5_VSC_UNLOCK);
	pci_cfg_access_unlock(dev->pdev);
	return ret;
}

int mlx5_vsc_gw_set_space(struct mlx5_core_dev *dev, u16 space,
			  u32 *ret_space_size)
{
	int ret;
	u32 val = 0;

	if (!mlx5_vsc_accessible(dev))
		return -EINVAL;

	if (ret_space_size)
		*ret_space_size = 0;

	/* Get a unique val */
	ret = vsc_read(dev, VSC_CTRL_OFFSET, &val);
	if (ret)
		goto out;

	/* Try to modify the lock */
	val = MLX5_MERGE(val, space, VSC_SPACE_BIT_OFFS, VSC_SPACE_BIT_LEN);
	ret = vsc_write(dev, VSC_CTRL_OFFSET, val);
	if (ret)
		goto out;

	/* Verify lock was modified */
	ret = vsc_read(dev, VSC_CTRL_OFFSET, &val);
	if (ret)
		goto out;

	if (MLX5_EXTRACT(val, VSC_STATUS_BIT_OFFS, VSC_STATUS_BIT_LEN) == 0)
		return -EINVAL;

	/* Get space max address if indicated by size valid bit */
	if (ret_space_size &&
	    MLX5_EXTRACT(val, VSC_SIZE_VLD_BIT_OFFS, VSC_SIZE_VLD_BIT_LEN)) {
		ret = vsc_read(dev, VSC_ADDR_OFFSET, &val);
		if (ret) {
			mlx5_core_warn(dev, "Failed to get max space size\n");
			goto out;
		}
		*ret_space_size = MLX5_EXTRACT(val, VSC_ADDR_BIT_OFFS,
					       VSC_ADDR_BIT_LEN);
	}
	return 0;

out:
	return ret;
}

static int mlx5_vsc_wait_on_flag(struct mlx5_core_dev *dev, u8 expected_val)
{
	int retries = 0;
	u32 flag;
	int ret;

	do {
		if (retries > VSC_MAX_RETRIES)
			return -EBUSY;

		ret = vsc_read(dev, VSC_ADDR_OFFSET, &flag);
		if (ret)
			return ret;
		flag = MLX5_EXTRACT(flag, VSC_FLAG_BIT_OFFS, VSC_FLAG_BIT_LEN);
		retries++;

		if ((retries & 0xf) == 0)
			usleep_range(1000, 2000);

	} while (flag != expected_val);

	return 0;
}

static int mlx5_vsc_gw_write(struct mlx5_core_dev *dev, unsigned int address,
			     u32 data)
{
	int ret;

	if (MLX5_EXTRACT(address, VSC_SYND_BIT_OFFS,
			 VSC_FLAG_BIT_LEN + VSC_SYND_BIT_LEN))
		return -EINVAL;

	/* Set flag to 0x1 */
	address = MLX5_MERGE(address, 1, VSC_FLAG_BIT_OFFS, 1);
	ret = vsc_write(dev, VSC_DATA_OFFSET, data);
	if (ret)
		goto out;

	ret = vsc_write(dev, VSC_ADDR_OFFSET, address);
	if (ret)
		goto out;

	/* Wait for the flag to be cleared */
	ret = mlx5_vsc_wait_on_flag(dev, 0);

out:
	return ret;
}

static int mlx5_vsc_gw_read(struct mlx5_core_dev *dev, unsigned int address,
			    u32 *data)
{
	int ret;

	if (MLX5_EXTRACT(address, VSC_SYND_BIT_OFFS,
			 VSC_FLAG_BIT_LEN + VSC_SYND_BIT_LEN))
		return -EINVAL;

	ret = vsc_write(dev, VSC_ADDR_OFFSET, address);
	if (ret)
		goto out;

	ret = mlx5_vsc_wait_on_flag(dev, 1);
	if (ret)
		goto out;

	ret = vsc_read(dev, VSC_DATA_OFFSET, data);
out:
	return ret;
}

static int mlx5_vsc_gw_read_fast(struct mlx5_core_dev *dev,
				 unsigned int read_addr,
				 unsigned int *next_read_addr,
				 u32 *data)
{
	int ret;

	ret = mlx5_vsc_gw_read(dev, read_addr, data);
	if (ret)
		goto out;

	ret = vsc_read(dev, VSC_ADDR_OFFSET, next_read_addr);
	if (ret)
		goto out;

	*next_read_addr = MLX5_EXTRACT(*next_read_addr, VSC_ADDR_BIT_OFFS,
				       VSC_ADDR_BIT_LEN);

	if (*next_read_addr <= read_addr)
		ret = -EINVAL;
out:
	return ret;
}

int mlx5_vsc_gw_read_block_fast(struct mlx5_core_dev *dev, u32 *data,
				int length)
{
	unsigned int next_read_addr = 0;
	unsigned int read_addr = 0;

	while (read_addr < length) {
		if (mlx5_vsc_gw_read_fast(dev, read_addr, &next_read_addr,
					  &data[(read_addr >> 2)]))
			return read_addr;

		read_addr = next_read_addr;
	}
	return length;
}
