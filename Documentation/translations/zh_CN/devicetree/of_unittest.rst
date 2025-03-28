.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/devicetree/of_unittest.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

=================================
Open Firmware Devicetree 单元测试
=================================

作者: Gaurav Minocha <gaurav.minocha.os@gmail.com>

1. 概述
=======

本文档解释了执行 OF 单元测试所需的测试数据是如何动态地附加到实时树上的，与机器的架构无关。

建议在继续读下去之前，先阅读以下文件。

(1) Documentation/devicetree/usage-model.rst
(2) http://www.devicetree.org/Device_Tree_Usage

OF Selftest被设计用来测试提供给设备驱动开发者的接口（include/linux/of.h），以从未扁平
化的设备树数据结构中获取设备信息等。这个接口被大多数设备驱动在各种使用情况下使用。


2. 测试数据
===========

设备树源文件（drivers/of/unittest-data/testcases.dts）包含执行drivers/of/unittest.c
中自动化单元测试所需的测试数据。目前，以下设备树源包含文件（.dtsi）被包含在testcases.dt中::

    drivers/of/unittest-data/tests-interrupts.dtsi
    drivers/of/unittest-data/tests-platform.dtsi
    drivers/of/unittest-data/tests-phandle.dtsi
    drivers/of/unittest-data/tests-match.dtsi

当内核在启用CONFIG_OF_UNITTEST的情况下被构建时，那么下面的make规则::

    $(obj)/%.dtb: $(src)/%.dts FORCE
	    $(call if_changed_dep, dtc)

用于将DT源文件（testcases.dts）编译成二进制blob（testcases.dtb），也被称为扁平化的DT。

之后，使用以下规则将上述二进制blob包装成一个汇编文件（testcases.dtb.S）::

    $(obj)/%.dtb.S: $(obj)/%.dtb
	    $(call cmd, dt_S_dtb)

汇编文件被编译成一个对象文件（testcases.dtb.o），并被链接到内核镜像中。


2.1. 添加测试数据
-----------------

未扁平化的设备树结构体:

未扁平化的设备树由连接的设备节点组成，其树状结构形式如下所述::

    // following struct members are used to construct the tree
    struct device_node {
	...
	struct  device_node *parent;
	struct  device_node *child;
	struct  device_node *sibling;
	...
    };

图1描述了一个机器的未扁平化设备树的通用结构，只考虑了子节点和同级指针。存在另一个指针，
``*parent`` ，用于反向遍历该树。因此，在一个特定的层次上，子节点和所有的兄弟姐妹节点将
有一个指向共同节点的父指针（例如，child1、sibling2、sibling3、sibling4的父指针指向
根节点）::

    root ('/')
    |
    child1 -> sibling2 -> sibling3 -> sibling4 -> null
    |         |           |           |
    |         |           |          null
    |         |           |
    |         |        child31 -> sibling32 -> null
    |         |           |          |
    |         |          null       null
    |         |
    |      child21 -> sibling22 -> sibling23 -> null
    |         |          |            |
    |        null       null         null
    |
    child11 -> sibling12 -> sibling13 -> sibling14 -> null
    |           |           |            |
    |           |           |           null
    |           |           |
    null        null       child131 -> null
			    |
			    null

Figure 1: 未扁平化的设备树的通用结构


在执行OF单元测试之前，需要将测试数据附加到机器的设备树上（如果存在）。因此，当调用
selftest_data_add()时，首先会读取通过以下内核符号链接到内核镜像中的扁平化设备树
数据::

    __dtb_testcases_begin - address marking the start of test data blob
    __dtb_testcases_end   - address marking the end of test data blob

其次，它调用of_fdt_unflatten_tree()来解除扁平化的blob。最后，如果机器的设备树
（即实时树）是存在的，那么它将未扁平化的测试数据树附加到实时树上，否则它将自己作为
实时设备树附加。

attach_node_and_children()使用of_attach_node()将节点附加到实时树上，如下所
述。为了解释这一点，图2中描述的测试数据树被附加到图1中描述的实时树上::

    root ('/')
	|
    testcase-data
	|
    test-child0 -> test-sibling1 -> test-sibling2 -> test-sibling3 -> null
	|               |                |                |
    test-child01      null             null             null


Figure 2: 将测试数据树附在实时树上的例子。

根据上面的方案，实时树已经存在，所以不需要附加根('/')节点。所有其他节点都是通过在
每个节点上调用of_attach_node()来附加的。

在函数of_attach_node()中，新的节点被附在实时树中给定的父节点的子节点上。但是，如
果父节点已经有了一个孩子，那么新节点就会取代当前的孩子，并将其变成其兄弟姐妹。因此，
当测试案例的数据节点被连接到上面的实时树（图1）时，最终的结构如图3所示::

    root ('/')
    |
    testcase-data -> child1 -> sibling2 -> sibling3 -> sibling4 -> null
    |               |          |           |           |
    (...)             |          |           |          null
		    |          |         child31 -> sibling32 -> null
		    |          |           |           |
		    |          |          null        null
		    |          |
		    |        child21 -> sibling22 -> sibling23 -> null
		    |          |           |            |
		    |         null        null         null
		    |
		    child11 -> sibling12 -> sibling13 -> sibling14 -> null
		    |          |            |            |
		    null       null          |           null
					    |
					    child131 -> null
					    |
					    null
    -----------------------------------------------------------------------

    root ('/')
    |
    testcase-data -> child1 -> sibling2 -> sibling3 -> sibling4 -> null
    |               |          |           |           |
    |             (...)      (...)       (...)        null
    |
    test-sibling3 -> test-sibling2 -> test-sibling1 -> test-child0 -> null
    |                |                   |                |
    null             null                null         test-child01


Figure 3: 附加测试案例数据后的实时设备树结构。


聪明的读者会注意到，与先前的结构相比，test-child0节点成为最后一个兄弟姐妹（图2）。
在连接了第一个test-child0节点之后，又连接了test-sibling1节点，该节点推动子节点
（即test-child0）成为兄弟姐妹，并使自己成为子节点，如上所述。

如果发现一个重复的节点（即如果一个具有相同full_name属性的节点已经存在于实时树中），
那么该节点不会被附加，而是通过调用函数update_node_properties()将其属性更新到活
树的节点中。


2.2. 删除测试数据
-----------------

一旦测试用例执行完，selftest_data_remove被调用，以移除最初连接的设备节点（首先是
叶子节点被分离，然后向上移动父节点被移除，最后是整个树）。selftest_data_remove()
调用detach_node_and_children()，使用of_detach_node()将节点从实时设备树上分离。

为了分离一个节点，of_detach_node()要么将给定节点的父节点的子节点指针更新为其同级节
点，要么根据情况将前一个同级节点附在给定节点的同级节点上。就这样吧。 :)
