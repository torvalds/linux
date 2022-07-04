:Original: Documentation/vm/overcommit-accounting.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:



==============
超量使用审计
==============

Linux内核支持下列超量使用处理模式

0
	启发式超量使用处理。拒绝明显的地址空间超量使用。用于一个典型的系统。
	它确保严重的疯狂分配失败，同时允许超量使用以减少swap的使用。在这种模式下，
	允许root分配稍多的内存。这是默认的。
1
	总是超量使用。适用于一些科学应用。经典的例子是使用稀疏数组的代码，只是依赖
	几乎完全由零页组成的虚拟内存

2
	不超量使用。系统提交的总地址空间不允许超过swap+一个可配置的物理RAM的数量
	（默认为50%）。根据你使用的数量，在大多数情况下，这意味着一个进程在访问页面时
	不会被杀死，但会在内存分配上收到相应的错误。

	对于那些想保证他们的内存分配在未来可用而又不需要初始化每一个页面的应用程序来说
	是很有用的。

超量使用策略是通过sysctl  `vm.overcommit_memory` 设置的。

可以通过 `vm.overcommit_ratio` （百分比）或 `vm.overcommit_kbytes` （绝对值）
来设置超限数量。这些只有在 `vm.overcommit_memory` 被设置为2时才有效果。

在 ``/proc/meminfo`` 中可以分别以CommitLimit和Committed_AS的形式查看当前
的超量使用和提交量。

陷阱
====

C语言的堆栈增长是一个隐含的mremap。如果你想得到绝对的保证，并在接近边缘的地方运行，
你 **必须** 为你认为你需要的最大尺寸的堆栈进行mmap。对于典型的堆栈使用来说，这并
不重要，但如果你真的非常关心的话，这就是一个值得关注的案例。


在模式2中，MAP_NORESERVE标志被忽略。


它是如何工作的
==============

超量使用是基于以下规则

对于文件映射
	| SHARED or READ-only	-	0 cost (该文件是映射而不是交换)
	| PRIVATE WRITABLE	-	每个实例的映射大小

对于匿名或者 ``/dev/zero`` 映射
	| SHARED			-	映射的大小
	| PRIVATE READ-only	-	0 cost (但作用不大)
	| PRIVATE WRITABLE	-	每个实例的映射大小

额外的计数
	| 通过mmap制作可写副本的页面
	| 从同一池中提取的shmfs内存

状态
====

*	我们核算mmap内存映射
*	我们核算mprotect在提交中的变化
*	我们核算mremap的大小变化
*	我们的审计 brk
*	审计munmap
*	我们在/proc中报告commit 状态
*	核对并检查分叉的情况
*	审查堆栈处理/执行中的构建
*	叙述SHMfs的情况
*	实现实际限制的执行

待续
====
*	ptrace 页计数（这很难）。
