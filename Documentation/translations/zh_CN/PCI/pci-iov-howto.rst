.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/PCI/pci-iov-howto.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:



.. _cn_pci-iov-howto:

==========================
PCI Express I/O 虚拟化指南
==========================

:版权: |copy| 2009 Intel Corporation
:作者: - Yu Zhao <yu.zhao@intel.com>
          - Donald Dutile <ddutile@redhat.com>

概述
====

什么是SR-IOV
------------

单根I/O虚拟化（SR-IOV）是一种PCI Express扩展功能，它使一个物理设备显示为多个
虚拟设备。物理设备被称为物理功能（PF），而虚拟设备被称为虚拟功能（VF）。VF的分
配可以由PF通过封装在该功能中的寄存器动态控制。默认情况下，该功能未被启用，PF表
现为传统的PCIe设备。一旦开启，每个VF的PCI配置空间都可以通过自己的总线、设备和
功能编号（路由ID）来访问。而且每个VF也有PCI内存空间，用于映射其寄存器集。VF设
备驱动程序对寄存器集进行操作，这样它就可以发挥功能，并作为一个真正的现有PCI设备
出现。

使用指南
========

我怎样才能启用SR-IOV功能
------------------------

有多种方法可用于SR-IOV的启用。在第一种方法中，设备驱动（PF驱动）将通过SR-IOV
核心提供的API控制功能的启用和禁用。如果硬件具有SR-IOV能力，加载其PF驱动器将启
用它和与PF相关的所有VF。一些PF驱动需要设置一个模块参数，以确定要启用的VF的数量。
在第二种方法中，对sysfs文件sriov_numvfs的写入将启用和禁用与PCIe PF相关的VF。
这种方法实现了每个PF的VF启用/禁用值，而第一种方法则适用于同一设备的所有PF。此外，
PCI SRIOV核心支持确保启用/禁用操作是有效的，以减少同一检查在多个驱动程序中的重
复，例如，如果启用VF，检查numvfs == 0，确保numvfs <= totalvfs。
第二种方法是对新的/未来的VF设备的推荐方法。

我怎样才能使用虚拟功能
----------------------

在内核中，VF被视为热插拔的PCI设备，所以它们应该能够以与真正的PCI设备相同的方式
工作。VF需要的设备驱动与普通PCI设备的驱动相同。

开发者指南
==========

SR-IOV API
----------

用来开启SR-IOV功能:

(a) 对于第一种方法，在驱动程序中::

	int pci_enable_sriov(struct pci_dev *dev, int nr_virtfn);

nr_virtfn'是要启用的VF的编号。

(b) 对于第二种方法，从sysfs::

	echo 'nr_virtfn' > \
        /sys/bus/pci/devices/<DOMAIN:BUS:DEVICE.FUNCTION>/sriov_numvfs

用来关闭SR-IOV功能:

(a) 对于第一种方法，在驱动程序中::

	void pci_disable_sriov(struct pci_dev *dev);

(b) 对于第二种方法，从sysfs::

	echo  0 > \
        /sys/bus/pci/devices/<DOMAIN:BUS:DEVICE.FUNCTION>/sriov_numvfs

要想通过主机上的兼容驱动启用自动探测VF，在启用SR-IOV功能之前运行下面的命令。这
是默认的行为。
::

	echo 1 > \
        /sys/bus/pci/devices/<DOMAIN:BUS:DEVICE.FUNCTION>/sriov_drivers_autoprobe

要禁止主机上的兼容驱动自动探测VF，请在启用SR-IOV功能之前运行以下命令。更新这个
入口不会影响已经被探测的VF。
::

	echo  0 > \
        /sys/bus/pci/devices/<DOMAIN:BUS:DEVICE.FUNCTION>/sriov_drivers_autoprobe

用例
----

下面的代码演示了SR-IOV API的用法
::

	static int dev_probe(struct pci_dev *dev, const struct pci_device_id *id)
	{
		pci_enable_sriov(dev, NR_VIRTFN);

		...

		return 0;
	}

	static void dev_remove(struct pci_dev *dev)
	{
		pci_disable_sriov(dev);

		...
	}

	static int dev_suspend(struct pci_dev *dev, pm_message_t state)
	{
		...

		return 0;
	}

	static int dev_resume(struct pci_dev *dev)
	{
		...

		return 0;
	}

	static void dev_shutdown(struct pci_dev *dev)
	{
		...
	}

	static int dev_sriov_configure(struct pci_dev *dev, int numvfs)
	{
		if (numvfs > 0) {
			...
			pci_enable_sriov(dev, numvfs);
			...
			return numvfs;
		}
		if (numvfs == 0) {
			....
			pci_disable_sriov(dev);
			...
			return 0;
		}
	}

	static struct pci_driver dev_driver = {
		.name =		"SR-IOV Physical Function driver",
		.id_table =	dev_id_table,
		.probe =	dev_probe,
		.remove =	dev_remove,
		.suspend =	dev_suspend,
		.resume =	dev_resume,
		.shutdown =	dev_shutdown,
		.sriov_configure = dev_sriov_configure,
	};
