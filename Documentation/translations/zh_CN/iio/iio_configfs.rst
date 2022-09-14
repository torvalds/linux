.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/iio/iio_configfs.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_iio_configfs:

=====================
工业 IIO configfs支持
=====================

1. 概述
=======

Configfs是一种内核对象的基于文件系统的管理系统，IIO使用一些可以通过
configfs轻松配置的对象（例如：设备，触发器）。

关于configfs是如何运行的，请查阅Documentation/filesystems/configfs.rst
了解更多信息。

2. 用法
=======
为了使configfs支持IIO，我们需要在编译时选中config的CONFIG_IIO_CONFIGFS
选项。

然后，挂载configfs文件系统(通常在 /config directory目录下）：：

  $ mkdir/config
  $ mount -t configfs none/config

此时，将创建所有默认IIO组，并可以在/ config / iio下对其进行访问。 下一章
将介绍可用的IIO配置对象。

3. 软件触发器
=============

IIO默认configfs组之一是“触发器”组。挂载configfs后可以自动访问它，并且可
以在/config/iio/triggers下找到。

IIO软件触发器为创建多种触发器类型提供了支持。通常在include/linux/iio
/sw_trigger.h：中的接口下将新的触发器类型实现为单独的内核模块：
::

  /*
   * drivers/iio/trigger/iio-trig-sample.c
   * 一种新触发器类型的内核模块实例
   */
  #include <linux/iio/sw_trigger.h>


  static struct iio_sw_trigger *iio_trig_sample_probe(const char *name)
  {
	/*
	 * 这将分配并注册一个IIO触发器以及其他触发器类型特性的初始化。
	 */
  }

  static int iio_trig_sample_remove(struct iio_sw_trigger *swt)
  {
	/*
	 * 这会废弃iio_trig_sample_probe中的操作
	 */
  }

  static const struct iio_sw_trigger_ops iio_trig_sample_ops = {
	.probe		= iio_trig_sample_probe,
	.remove		= iio_trig_sample_remove,
  };

  static struct iio_sw_trigger_type iio_trig_sample = {
	.name = "trig-sample",
	.owner = THIS_MODULE,
	.ops = &iio_trig_sample_ops,
  };

  module_iio_sw_trigger_driver(iio_trig_sample);

每种触发器类型在/config/iio/triggers下都有其自己的目录。加载iio-trig-sample
模块将创建“trig-sample”触发器类型目录/config/iio/triggers/trig-sample.

我们支持以下中断源（触发器类型）

	* hrtimer,使用高分辨率定时器作为中断源

3.1 Hrtimer触发器创建与销毁
---------------------------

加载iio-trig-hrtimer模块将注册hrtimer触发器类型，从而允许用户在
/config/iio/triggers/hrtimer下创建hrtimer触发器。

例如::

  $ mkdir /config/iio/triggers/hrtimer/instance1
  $ rmdir /config/iio/triggers/hrtimer/instance1

每个触发器可以具有一个或多个独特的触发器类型的属性。

3.2 "hrtimer" 触发器类型属性
----------------------------

"hrtimer”触发器类型没有来自/config dir的任何可配置属性。
它确实引入了触发目录的sampling_frequency属性。
该属性以Hz为单位设置轮询频率，精度为mHz。