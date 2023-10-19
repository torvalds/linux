/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INTEL_SCU_IPC_H_
#define  _ASM_X86_INTEL_SCU_IPC_H_

#include <linux/ioport.h>

struct device;
struct intel_scu_ipc_dev;

/**
 * struct intel_scu_ipc_data - Data used to configure SCU IPC
 * @mem: Base address of SCU IPC MMIO registers
 * @irq: The IRQ number used for SCU (optional)
 */
struct intel_scu_ipc_data {
	struct resource mem;
	int irq;
};

struct intel_scu_ipc_dev *
__intel_scu_ipc_register(struct device *parent,
			 const struct intel_scu_ipc_data *scu_data,
			 struct module *owner);

#define intel_scu_ipc_register(parent, scu_data)  \
	__intel_scu_ipc_register(parent, scu_data, THIS_MODULE)

void intel_scu_ipc_unregister(struct intel_scu_ipc_dev *scu);

struct intel_scu_ipc_dev *
__devm_intel_scu_ipc_register(struct device *parent,
			      const struct intel_scu_ipc_data *scu_data,
			      struct module *owner);

#define devm_intel_scu_ipc_register(parent, scu_data)  \
	__devm_intel_scu_ipc_register(parent, scu_data, THIS_MODULE)

struct intel_scu_ipc_dev *intel_scu_ipc_dev_get(void);
void intel_scu_ipc_dev_put(struct intel_scu_ipc_dev *scu);
struct intel_scu_ipc_dev *devm_intel_scu_ipc_dev_get(struct device *dev);

int intel_scu_ipc_dev_ioread8(struct intel_scu_ipc_dev *scu, u16 addr,
			      u8 *data);
int intel_scu_ipc_dev_iowrite8(struct intel_scu_ipc_dev *scu, u16 addr,
			       u8 data);
int intel_scu_ipc_dev_readv(struct intel_scu_ipc_dev *scu, u16 *addr,
			    u8 *data, size_t len);
int intel_scu_ipc_dev_writev(struct intel_scu_ipc_dev *scu, u16 *addr,
			     u8 *data, size_t len);

int intel_scu_ipc_dev_update(struct intel_scu_ipc_dev *scu, u16 addr,
			     u8 data, u8 mask);

int intel_scu_ipc_dev_simple_command(struct intel_scu_ipc_dev *scu, int cmd,
				     int sub);
int intel_scu_ipc_dev_command_with_size(struct intel_scu_ipc_dev *scu, int cmd,
					int sub, const void *in, size_t inlen,
					size_t size, void *out, size_t outlen);

static inline int intel_scu_ipc_dev_command(struct intel_scu_ipc_dev *scu, int cmd,
					    int sub, const void *in, size_t inlen,
					    void *out, size_t outlen)
{
	return intel_scu_ipc_dev_command_with_size(scu, cmd, sub, in, inlen,
						   inlen, out, outlen);
}

#endif
