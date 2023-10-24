// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2018 Google LLC
 * Copyright (c) 2021 Aspeed Technology Inc.
 */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

/* register offsets */
#define HICR9	0x98
#define HICRA	0x9c

/* attributes options */
#define UART_ROUTING_IO1	"io1"
#define UART_ROUTING_IO2	"io2"
#define UART_ROUTING_IO3	"io3"
#define UART_ROUTING_IO4	"io4"
#define UART_ROUTING_IO5	"io5"
#define UART_ROUTING_IO6	"io6"
#define UART_ROUTING_IO10	"io10"
#define UART_ROUTING_UART1	"uart1"
#define UART_ROUTING_UART2	"uart2"
#define UART_ROUTING_UART3	"uart3"
#define UART_ROUTING_UART4	"uart4"
#define UART_ROUTING_UART5	"uart5"
#define UART_ROUTING_UART6	"uart6"
#define UART_ROUTING_UART10	"uart10"
#define UART_ROUTING_RES	"reserved"

struct aspeed_uart_routing {
	struct regmap *map;
	struct attribute_group const *attr_grp;
};

struct aspeed_uart_routing_selector {
	struct device_attribute	dev_attr;
	uint8_t reg;
	uint8_t mask;
	uint8_t shift;
	const char *const options[];
};

#define to_routing_selector(_dev_attr)					\
	container_of(_dev_attr, struct aspeed_uart_routing_selector, dev_attr)

static ssize_t aspeed_uart_routing_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);

static ssize_t aspeed_uart_routing_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count);

#define ROUTING_ATTR(_name) {					\
	.attr = {.name = _name,					\
		 .mode = VERIFY_OCTAL_PERMISSIONS(0644) },	\
	.show = aspeed_uart_routing_show,			\
	.store = aspeed_uart_routing_store,			\
}

/* routing selector for AST25xx */
static struct aspeed_uart_routing_selector ast2500_io6_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO6),
	.reg = HICR9,
	.shift = 8,
	.mask = 0xf,
	.options = {
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART5,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO5,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2500_uart5_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART5),
	.reg = HICRA,
	.shift = 28,
	.mask = 0xf,
	.options = {
		    UART_ROUTING_IO5,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_IO6,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2500_uart4_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART4),
	.reg = HICRA,
	.shift = 25,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_IO6,
		    NULL,
	},
};

static struct aspeed_uart_routing_selector ast2500_uart3_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART3),
	.reg = HICRA,
	.shift = 22,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_IO6,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2500_uart2_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART2),
	.reg = HICRA,
	.shift = 19,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO1,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART1,
		    UART_ROUTING_IO6,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2500_uart1_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART1),
	.reg = HICRA,
	.shift = 16,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_IO6,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2500_io5_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO5),
	.reg = HICRA,
	.shift = 12,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_UART5,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO6,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2500_io4_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO4),
	.reg = HICRA,
	.shift = 9,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART5,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO6,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2500_io3_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO3),
	.reg = HICRA,
	.shift = 6,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART5,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO6,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2500_io2_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO2),
	.reg = HICRA,
	.shift = 3,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART5,
		    UART_ROUTING_UART1,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO6,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2500_io1_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO1),
	.reg = HICRA,
	.shift = 0,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART5,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO6,
		    NULL,
		    },
};

static struct attribute *ast2500_uart_routing_attrs[] = {
	&ast2500_io6_sel.dev_attr.attr,
	&ast2500_uart5_sel.dev_attr.attr,
	&ast2500_uart4_sel.dev_attr.attr,
	&ast2500_uart3_sel.dev_attr.attr,
	&ast2500_uart2_sel.dev_attr.attr,
	&ast2500_uart1_sel.dev_attr.attr,
	&ast2500_io5_sel.dev_attr.attr,
	&ast2500_io4_sel.dev_attr.attr,
	&ast2500_io3_sel.dev_attr.attr,
	&ast2500_io2_sel.dev_attr.attr,
	&ast2500_io1_sel.dev_attr.attr,
	NULL,
};

static const struct attribute_group ast2500_uart_routing_attr_group = {
	.attrs = ast2500_uart_routing_attrs,
};

/* routing selector for AST26xx */
static struct aspeed_uart_routing_selector ast2600_uart10_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART10),
	.reg = HICR9,
	.shift = 12,
	.mask = 0xf,
	.options = {
		    UART_ROUTING_IO10,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
			UART_ROUTING_RES,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2600_io10_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO10),
	.reg = HICR9,
	.shift = 8,
	.mask = 0xf,
	.options = {
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
			UART_ROUTING_RES,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
			UART_ROUTING_RES,
		    UART_ROUTING_UART10,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2600_uart4_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART4),
	.reg = HICRA,
	.shift = 25,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_IO10,
		    NULL,
	},
};

static struct aspeed_uart_routing_selector ast2600_uart3_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART3),
	.reg = HICRA,
	.shift = 22,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_IO10,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2600_uart2_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART2),
	.reg = HICRA,
	.shift = 19,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO1,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART1,
		    UART_ROUTING_IO10,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2600_uart1_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_UART1),
	.reg = HICRA,
	.shift = 16,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_IO10,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2600_io4_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO4),
	.reg = HICRA,
	.shift = 9,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART10,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO10,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2600_io3_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO3),
	.reg = HICRA,
	.shift = 6,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART10,
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_IO1,
		    UART_ROUTING_IO2,
		    UART_ROUTING_IO10,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2600_io2_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO2),
	.reg = HICRA,
	.shift = 3,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART10,
		    UART_ROUTING_UART1,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO10,
		    NULL,
		    },
};

static struct aspeed_uart_routing_selector ast2600_io1_sel = {
	.dev_attr = ROUTING_ATTR(UART_ROUTING_IO1),
	.reg = HICRA,
	.shift = 0,
	.mask = 0x7,
	.options = {
		    UART_ROUTING_UART1,
		    UART_ROUTING_UART2,
		    UART_ROUTING_UART3,
		    UART_ROUTING_UART4,
		    UART_ROUTING_UART10,
		    UART_ROUTING_IO3,
		    UART_ROUTING_IO4,
		    UART_ROUTING_IO10,
		    NULL,
		    },
};

static struct attribute *ast2600_uart_routing_attrs[] = {
	&ast2600_uart10_sel.dev_attr.attr,
	&ast2600_io10_sel.dev_attr.attr,
	&ast2600_uart4_sel.dev_attr.attr,
	&ast2600_uart3_sel.dev_attr.attr,
	&ast2600_uart2_sel.dev_attr.attr,
	&ast2600_uart1_sel.dev_attr.attr,
	&ast2600_io4_sel.dev_attr.attr,
	&ast2600_io3_sel.dev_attr.attr,
	&ast2600_io2_sel.dev_attr.attr,
	&ast2600_io1_sel.dev_attr.attr,
	NULL,
};

static const struct attribute_group ast2600_uart_routing_attr_group = {
	.attrs = ast2600_uart_routing_attrs,
};

static ssize_t aspeed_uart_routing_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct aspeed_uart_routing *uart_routing = dev_get_drvdata(dev);
	struct aspeed_uart_routing_selector *sel = to_routing_selector(attr);
	int val, pos, len;

	regmap_read(uart_routing->map, sel->reg, &val);
	val = (val >> sel->shift) & sel->mask;

	len = 0;
	for (pos = 0; sel->options[pos] != NULL; ++pos) {
		if (pos == val)
			len += sysfs_emit_at(buf, len, "[%s] ", sel->options[pos]);
		else
			len += sysfs_emit_at(buf, len, "%s ", sel->options[pos]);
	}

	if (val >= pos)
		len += sysfs_emit_at(buf, len, "[unknown(%d)]", val);

	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

static ssize_t aspeed_uart_routing_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct aspeed_uart_routing *uart_routing = dev_get_drvdata(dev);
	struct aspeed_uart_routing_selector *sel = to_routing_selector(attr);
	int val;

	val = __sysfs_match_string(sel->options, -1, buf);
	if (val < 0) {
		dev_err(dev, "invalid value \"%s\"\n", buf);
		return -EINVAL;
	}

	regmap_update_bits(uart_routing->map, sel->reg,
			(sel->mask << sel->shift),
			(val & sel->mask) << sel->shift);

	return count;
}

static int aspeed_uart_routing_probe(struct platform_device *pdev)
{
	int rc;
	struct device *dev = &pdev->dev;
	struct aspeed_uart_routing *uart_routing;

	uart_routing = devm_kzalloc(&pdev->dev, sizeof(*uart_routing), GFP_KERNEL);
	if (!uart_routing)
		return -ENOMEM;

	uart_routing->map = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(uart_routing->map)) {
		dev_err(dev, "cannot get regmap\n");
		return PTR_ERR(uart_routing->map);
	}

	uart_routing->attr_grp = of_device_get_match_data(dev);

	rc = sysfs_create_group(&dev->kobj, uart_routing->attr_grp);
	if (rc < 0)
		return rc;

	dev_set_drvdata(dev, uart_routing);

	dev_info(dev, "module loaded\n");

	return 0;
}

static int aspeed_uart_routing_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_uart_routing *uart_routing = platform_get_drvdata(pdev);

	sysfs_remove_group(&dev->kobj, uart_routing->attr_grp);

	return 0;
}

static const struct of_device_id aspeed_uart_routing_table[] = {
	{ .compatible = "aspeed,ast2400-uart-routing",
	  .data = &ast2500_uart_routing_attr_group },
	{ .compatible = "aspeed,ast2500-uart-routing",
	  .data = &ast2500_uart_routing_attr_group },
	{ .compatible = "aspeed,ast2600-uart-routing",
	  .data = &ast2600_uart_routing_attr_group },
	{ },
};

static struct platform_driver aspeed_uart_routing_driver = {
	.driver = {
		.name = "aspeed-uart-routing",
		.of_match_table = aspeed_uart_routing_table,
	},
	.probe = aspeed_uart_routing_probe,
	.remove = aspeed_uart_routing_remove,
};

module_platform_driver(aspeed_uart_routing_driver);

MODULE_AUTHOR("Oskar Senft <osk@google.com>");
MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Driver to configure Aspeed UART routing");
