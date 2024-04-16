.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/protection-keys.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 吴想成 Wu XiangCheng <bobwxc@email.cn>

.. _cn_core-api_protection-keys:

============
内存保护密钥
============

用户空间的内存保护密钥（Memory Protection Keys for Userspace，PKU，亦
即PKEYs）是英特尔Skylake（及以后）“可扩展处理器”服务器CPU上的一项功能。
它将在未来的非服务器英特尔处理器和未来的AMD处理器中可用。

对于任何希望测试或使用该功能的人来说，它在亚马逊的EC2 C5实例中是可用的，
并且已知可以在那里使用Ubuntu 17.04镜像运行。

内存保护密钥提供了一种机制来执行基于页面的保护，但在应用程序改变保护域
时不需要修改页表。它的工作原理是在每个页表项中为“保护密钥”分配4个以
前被忽略的位，从而提供16个可能的密钥。

还有一个新的用户可访问寄存器（PKRU），为每个密钥提供两个单独的位（访
问禁止和写入禁止）。作为一个CPU寄存器，PKRU在本质上是线程本地的，可能
会给每个线程提供一套不同于其他线程的保护措施。

有两条新指令（RDPKRU/WRPKRU）用于读取和写入新的寄存器。该功能仅在64位
模式下可用，尽管物理地址扩展页表中理论上有空间。这些权限只在数据访问上
强制执行，对指令获取没有影响。


系统调用
========

有3个系统调用可以直接与pkeys进行交互::

	int pkey_alloc(unsigned long flags, unsigned long init_access_rights)
	int pkey_free(int pkey);
	int pkey_mprotect(unsigned long start, size_t len,
			  unsigned long prot, int pkey);

在使用一个pkey之前，必须先用pkey_alloc()分配它。一个应用程序直接调用
WRPKRU指令，以改变一个密钥覆盖的内存的访问权限。在这个例子中，WRPKRU
被一个叫做pkey_set()的C函数所封装::

	int real_prot = PROT_READ|PROT_WRITE;
	pkey = pkey_alloc(0, PKEY_DISABLE_WRITE);
	ptr = mmap(NULL, PAGE_SIZE, PROT_NONE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	ret = pkey_mprotect(ptr, PAGE_SIZE, real_prot, pkey);
	... application runs here

现在，如果应用程序需要更新'ptr'处的数据，它可以获得访问权，进行更新，
然后取消其写访问权::

	pkey_set(pkey, 0); // clear PKEY_DISABLE_WRITE
	*ptr = foo; // assign something
	pkey_set(pkey, PKEY_DISABLE_WRITE); // set PKEY_DISABLE_WRITE again

现在，当它释放内存时，它也将释放pkey，因为它不再被使用了::

	munmap(ptr, PAGE_SIZE);
	pkey_free(pkey);

.. note:: pkey_set()是RDPKRU和WRPKRU指令的一个封装器。在tools/testing/selftests/x86/protection_keys.c中可以找到一个实现实例。
          tools/testing/selftests/x86/protection_keys.c.

行为
====

内核试图使保护密钥与普通的mprotect()的行为一致。例如，如果你这样做::

	mprotect(ptr, size, PROT_NONE);
	something(ptr);

这样做的时候，你可以期待保护密钥的相同效果::

	pkey = pkey_alloc(0, PKEY_DISABLE_WRITE | PKEY_DISABLE_READ);
	pkey_mprotect(ptr, size, PROT_READ|PROT_WRITE, pkey);
	something(ptr);

无论something()是否是对'ptr'的直接访问，这都应该为真。
如::

	*ptr = foo;

或者当内核代表应用程序进行访问时，比如read()::

	read(fd, ptr, 1);

在这两种情况下，内核都会发送一个SIGSEGV，但当违反保护密钥时，si_code
将被设置为SEGV_PKERR，而当违反普通的mprotect()权限时，则是SEGV_ACCERR。
