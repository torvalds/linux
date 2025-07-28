/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

 */

/*
	Module: rt2x00soc
	Abstract: Data structures for the rt2x00soc module.
 */

#ifndef RT2X00SOC_H
#define RT2X00SOC_H

/*
 * SoC driver handlers.
 */
int rt2x00soc_probe(struct platform_device *pdev, const struct rt2x00_ops *ops);
void rt2x00soc_remove(struct platform_device *pdev);
#ifdef CONFIG_PM
int rt2x00soc_suspend(struct platform_device *pdev, pm_message_t state);
int rt2x00soc_resume(struct platform_device *pdev);
#else
#define rt2x00soc_suspend	NULL
#define rt2x00soc_resume	NULL
#endif /* CONFIG_PM */

#endif /* RT2X00SOC_H */
