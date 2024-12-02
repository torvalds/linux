:Original: Documentation/mm/page_owner.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:


================================
page owner: 跟踪谁分配的每个页面
================================

概述
====

page owner是用来追踪谁分配的每一个页面。它可以用来调试内存泄漏或找到内存占用者。
当分配发生时，有关分配的信息，如调用堆栈和页面的顺序被存储到每个页面的特定存储中。
当我们需要了解所有页面的状态时，我们可以获得并分析这些信息。

尽管我们已经有了追踪页面分配/释放的tracepoint，但用它来分析谁分配的每个页面是
相当复杂的。我们需要扩大跟踪缓冲区，以防止在用户空间程序启动前出现重叠。而且，启
动的程序会不断地将跟踪缓冲区转出，供以后分析，这将会改变系统的行为，会产生更多的
可能性，而不是仅仅保留在内存中，所以不利于调试。

页面所有者也可以用于各种目的。例如，可以通过每个页面的gfp标志信息获得精确的碎片
统计。如果启用了page owner，它就已经实现并激活了。我们非常欢迎其他用途。

它也可以用来显示所有的栈以及它们当前分配的基础页面数，这让我们能够快速了解内存的
使用情况，而无需浏览所有页面并匹配分配和释放操作。

page owner在默认情况下是禁用的。所以，如果你想使用它，你需要在你的启动cmdline
中加入"page_owner=on"。如果内核是用page owner构建的，并且由于没有启用启动
选项而在运行时禁用page owner，那么运行时的开销是很小的。如果在运行时禁用，它不
需要内存来存储所有者信息，所以没有运行时内存开销。而且，页面所有者在页面分配器的
热路径中只插入了两个不可能的分支，如果不启用，那么分配就会像没有页面所有者的内核
一样进行。这两个不可能的分支应该不会影响到分配的性能，特别是在静态键跳转标签修补
功能可用的情况下。以下是由于这个功能而导致的内核代码大小的变化。

尽管启用page owner会使内核的大小增加几千字节，但这些代码大部分都在页面分配器和
热路径之外。构建带有page owner的内核，并在需要时打开它，将是调试内核内存问题的
最佳选择。

有一个问题是由实现细节引起的。页所有者将信息存储到struct page扩展的内存中。这
个内存的初始化时间比稀疏内存系统中的页面分配器启动的时间要晚一些，所以，在初始化
之前，许多页面可以被分配，但它们没有所有者信息。为了解决这个问题，这些早期分配的
页面在初始化阶段被调查并标记为分配。虽然这并不意味着它们有正确的所有者信息，但至
少，我们可以更准确地判断该页是否被分配。在2GB内存的x86-64虚拟机上，有13343
个早期分配的页面被捕捉和标记，尽管它们大部分是由结构页扩展功能分配的。总之，在这
之后，没有任何页面处于未追踪状态。

使用方法
========

1) 构建用户空间的帮助::

	cd tools/mm
	make page_owner_sort

2) 启用page owner: 添加 "page_owner=on" 到 boot cmdline.

3) 做你想调试的工作。

4) 分析来自页面所有者的信息::

	cat /sys/kernel/debug/page_owner_stacks/show_stacks > stacks.txt
	cat stacks.txt
	 post_alloc_hook+0x177/0x1a0
	 get_page_from_freelist+0xd01/0xd80
	 __alloc_pages+0x39e/0x7e0
	 allocate_slab+0xbc/0x3f0
	 ___slab_alloc+0x528/0x8a0
	 kmem_cache_alloc+0x224/0x3b0
	 sk_prot_alloc+0x58/0x1a0
	 sk_alloc+0x32/0x4f0
	 inet_create+0x427/0xb50
	 __sock_create+0x2e4/0x650
	 inet_ctl_sock_create+0x30/0x180
	 igmp_net_init+0xc1/0x130
	 ops_init+0x167/0x410
	 setup_net+0x304/0xa60
	 copy_net_ns+0x29b/0x4a0
	 create_new_namespaces+0x4a1/0x820
	nr_base_pages: 16
	...
	...
	echo 7000 > /sys/kernel/debug/page_owner_stacks/count_threshold
	cat /sys/kernel/debug/page_owner_stacks/show_stacks> stacks_7000.txt
	cat stacks_7000.txt
	 post_alloc_hook+0x177/0x1a0
	 get_page_from_freelist+0xd01/0xd80
	 __alloc_pages+0x39e/0x7e0
	 alloc_pages_mpol+0x22e/0x490
	 folio_alloc+0xd5/0x110
	 filemap_alloc_folio+0x78/0x230
	 page_cache_ra_order+0x287/0x6f0
	 filemap_get_pages+0x517/0x1160
	 filemap_read+0x304/0x9f0
	 xfs_file_buffered_read+0xe6/0x1d0 [xfs]
	 xfs_file_read_iter+0x1f0/0x380 [xfs]
	 __kernel_read+0x3b9/0x730
	 kernel_read_file+0x309/0x4d0
	 __do_sys_finit_module+0x381/0x730
	 do_syscall_64+0x8d/0x150
	 entry_SYSCALL_64_after_hwframe+0x62/0x6a
	nr_base_pages: 20824
	...

	cat /sys/kernel/debug/page_owner > page_owner_full.txt
	./page_owner_sort page_owner_full.txt sorted_page_owner.txt

   ``page_owner_full.txt`` 的一般输出情况如下::

	Page allocated via order XXX, ...
	PFN XXX ...
	// 栈详情

	Page allocated via order XXX, ...
	PFN XXX ...
	// 栈详情
    默认情况下，它将以一个给定的pfn开始，做完整的pfn转储，且page_owner支持fseek。

    FILE *fp = fopen("/sys/kernel/debug/page_owner", "r");
    fseek(fp, pfn_start, SEEK_SET);

   ``page_owner_sort`` 工具忽略了 ``PFN`` 行，将剩余的行放在buf中，使用regexp提
   取页序值，计算buf的次数和页数，最后根据参数进行排序。

   在 ``sorted_page_owner.txt`` 中可以看到关于谁分配了每个页面的结果。一般输出::

	XXX times, XXX pages:
	Page allocated via order XXX, ...
	// Detailed stack

   默认情况下， ``page_owner_sort`` 是根据buf的时间来排序的。如果你想
   按buf的页数排序，请使用-m参数。详细的参数是:

   基本函数::

	排序:
		-a		按内存分配时间排序
		-m		按总内存排序
		-p		按pid排序。
		-P		按tgid排序。
		-n		按任务命令名称排序。
		-r		按内存释放时间排序。
		-s		按堆栈跟踪排序。
		-t		按时间排序（默认）。
       --sort <order> 指定排序顺序。排序的语法是[+|-]key[,[+|-]key[,...]]。从
       **标准格式指定器**那一节选择一个键。"+"是可选的，因为默认的方向是数字或
       词法的增加。允许混合使用缩写和完整格式的键。

        例子:
				./page_owner_sort <input> <output> --sort=n,+pid,-tgid
				./page_owner_sort <input> <output> --sort=at

    其它函数::

	剔除:
		--cull <rules>
		        指定剔除规则。剔除的语法是key[,key[,...]]。从**标准格式指定器**
				部分选择一个多字母键。
		<rules>是一个以逗号分隔的列表形式的单一参数，它提供了一种指定单个剔除规则的
		方法。 识别的关键字在下面的**标准格式指定器**部分有描述。<规则>可以通过键的
		序列k1,k2,...来指定，在下面的标准排序键部分有描述。允许混合使用简写和完整形
		式的键。

		Examples:
				./page_owner_sort <input> <output> --cull=stacktrace
				./page_owner_sort <input> <output> --cull=st,pid,name
				./page_owner_sort <input> <output> --cull=n,f

	过滤:
		-f		过滤掉内存已被释放的块的信息。

	选择:
		--pid <pidlist>		按pid选择。这将选择进程ID号出现在<pidlist>中的块。
		--tgid <tgidlist>	按tgid选择。这将选择其线程组ID号出现在<tgidlist>
		                    中的块。
		--name <cmdlist>	按任务命令名称选择。这将选择其任务命令名称出现在
		                    <cmdlist>中的区块。

		<pidlist>, <tgidlist>, <cmdlist>是以逗号分隔的列表形式的单个参数，
		它提供了一种指定单个选择规则的方法。


		例子:
				./page_owner_sort <input> <output> --pid=1
				./page_owner_sort <input> <output> --tgid=1,2,3
				./page_owner_sort <input> <output> --name name1,name2

标准格式指定器
==============
::

  --sort的选项:

	短键		长键		描述
	p		pid		进程ID
	tg		tgid		线程组ID
	n		name		任务命令名称
	st		stacktrace	页面分配的堆栈跟踪
	T		txt		块的全文
	ft		free_ts		页面释放时的时间戳
	at		alloc_ts	页面被分配时的时间戳
	ator		allocator	页面的内存分配器

  --curl的选项:

	短键		长键		描述
	p		pid		进程ID
	tg		tgid		线程组ID
	n		name		任务命令名称
	f		free		该页是否已经释放
	st		stacktrace	页面分配的堆栈跟踪
	ator		allocator	页面的内存分配器
