/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _REALTEK_SMI_H
#define _REALTEK_SMI_H

#if IS_ENABLED(CONFIG_NET_DSA_REALTEK_SMI)

static inline int realtek_smi_driver_register(struct platform_driver *drv)
{
	return platform_driver_register(drv);
}

static inline void realtek_smi_driver_unregister(struct platform_driver *drv)
{
	platform_driver_unregister(drv);
}

int realtek_smi_probe(struct platform_device *pdev);
void realtek_smi_remove(struct platform_device *pdev);
void realtek_smi_shutdown(struct platform_device *pdev);

#else /* IS_ENABLED(CONFIG_NET_DSA_REALTEK_SMI) */

static inline int realtek_smi_driver_register(struct platform_driver *drv)
{
	return 0;
}

static inline void realtek_smi_driver_unregister(struct platform_driver *drv)
{
}

static inline int realtek_smi_probe(struct platform_device *pdev)
{
	return -ENOENT;
}

static inline void realtek_smi_remove(struct platform_device *pdev)
{
}

static inline void realtek_smi_shutdown(struct platform_device *pdev)
{
}

#endif /* IS_ENABLED(CONFIG_NET_DSA_REALTEK_SMI) */

#endif  /* _REALTEK_SMI_H */
