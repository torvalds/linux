.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/driver-api/io_ordering.rst

:翻译:

 林永听 Lin Yongting <linyongting@gmail.com>
 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

===========================
对内存映射地址的I/O写入排序
===========================

在某些平台上，所谓的内存映射I/O是弱顺序。在这些平台上，驱动开发者有责任
保证I/O内存映射地址的写操作按程序图意的顺序达到设备。通常读取一个“安全”
设备寄存器或桥寄存器，触发IO芯片清刷未处理的写操作到达设备后才处理读操作，
而达到保证目的。驱动程序通常在spinlock保护的临界区退出之前使用这种技术。
这也可以保证后面的写操作只在前面的写操作之后到达设备（这非常类似于内存
屏障操作，mb()，不过仅适用于I/O）。

假设一个设备驱动程的具体例子::

                ...
        CPU A:  spin_lock_irqsave(&dev_lock, flags)
        CPU A:  val = readl(my_status);
        CPU A:  ...
        CPU A:  writel(newval, ring_ptr);
        CPU A:  spin_unlock_irqrestore(&dev_lock, flags)
                ...
        CPU B:  spin_lock_irqsave(&dev_lock, flags)
        CPU B:  val = readl(my_status);
        CPU B:  ...
        CPU B:  writel(newval2, ring_ptr);
        CPU B:  spin_unlock_irqrestore(&dev_lock, flags)
                ...

上述例子中，设备可能会先接收到newval2的值，然后接收到newval的值，问题就
发生了。不过很容易通过下面方法来修复::

                ...
        CPU A:  spin_lock_irqsave(&dev_lock, flags)
        CPU A:  val = readl(my_status);
        CPU A:  ...
        CPU A:  writel(newval, ring_ptr);
        CPU A:  (void)readl(safe_register); /* 配置寄存器？*/
        CPU A:  spin_unlock_irqrestore(&dev_lock, flags)
                ...
        CPU B:  spin_lock_irqsave(&dev_lock, flags)
        CPU B:  val = readl(my_status);
        CPU B:  ...
        CPU B:  writel(newval2, ring_ptr);
        CPU B:  (void)readl(safe_register); /* 配置寄存器？*/
        CPU B:  spin_unlock_irqrestore(&dev_lock, flags)

在解决方案中，读取safe_register寄存器，触发IO芯片清刷未处理的写操作，
再处理后面的读操作，防止引发数据不一致问题。
