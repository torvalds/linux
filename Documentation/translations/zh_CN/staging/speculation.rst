.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/staging/speculation.rst

:翻译:

 崔巍 Cui Wei <chris.wei.cui@gmail.com>

========
推测执行
========

本文档解释了推测执行的潜在影响，以及如何使用通用API来减轻不良影响。

------------------------------------------------------------------------------

为提高性能并减少平均延迟，许多现代处理器都采用分支预测等推测执行技术，执行结果
可能在后续阶段被丢弃。

通常情况下，我们无法从架构状态（如寄存器内容）观察到推测执行。然而，在某些情况
下从微架构状态观察其影响是可能的，例如数据是否存在于缓存中。这种状态可能会形成
侧信道，通过观察侧信道可以提取秘密信息。

例如，在分支预测存在的情况下，边界检查可能被推测执行的代码忽略。考虑以下代码::

	int load_array(int *array, unsigned int index)
	{
		if (index >= MAX_ARRAY_ELEMS)
			return 0;
		else
			return array[index];
	}

在arm64上，可以编译成如下汇编序列::

	CMP	<index>, #MAX_ARRAY_ELEMS
	B.LT	less
	MOV	<returnval>, #0
	RET
  less:
	LDR	<returnval>, [<array>, <index>]
	RET

处理器有可能误预测条件分支，并推测性装载array[index]，即使index >= MAX_ARRAY_ELEMS。
这个值随后会被丢弃，但推测的装载可能会影响微架构状态，随后可被测量到。

涉及多个依赖内存访问的更复杂序列可能会导致敏感信息泄露。以前面的示例为基础，考虑
以下代码::

	int load_dependent_arrays(int *arr1, int *arr2, int index)
	{
		int val1, val2,

		val1 = load_array(arr1, index);
		val2 = load_array(arr2, val1);

		return val2;
	}

根据推测，对load_array()的第一次调用可能会返回一个越界地址的值，而第二次调用将影响
依赖于该值的微架构状态。这可能会提供一个任意读取原语。

缓解推测执行侧信道
==================

内核提供了一个通用API以确保即使在推测情况下也能遵守边界检查。受推测执行侧信道影响
的架构应当实现这些原语。

<linux/nospec.h>中的array_index_nospec()辅助函数可用于防止信息通过侧信道泄漏。

调用array_index_nospec(index, size)将返回一个经过净化的索引值，即使在CPU推测执行
条件下，该值也会被严格限制在[0, size)范围内。

这可以用来保护前面的load_array()示例::

	int load_array(int *array, unsigned int index)
	{
		if (index >= MAX_ARRAY_ELEMS)
			return 0;
		else {
			index = array_index_nospec(index, MAX_ARRAY_ELEMS);
			return array[index];
		}
	}
