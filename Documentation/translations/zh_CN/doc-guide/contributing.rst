.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/doc-guide/contributing.rst

:译者: 吴想成 Wu XiangCheng <bobwxc@email.cn>

如何帮助改进内核文档
====================

在任何软件开发项目中，文档都是重要组成部分。好的文档有助于引入新的开发人员，
并帮助已有的开发人员更有效地工作。如果缺少高质量的文档，大量的时间就会浪费在
代码的逆向工程和犯本可避免的错误上。

不幸的是，内核的文档目前远远不能满足支持如此规模和重要性的项目的需要。

本指南适用于希望帮助改善这种状况的贡献者。内核文档的改进可以由开发者在不同的
技能层级上进行；这也是一条相对简单可以帮助您了解内核过程并在社区中找到一席之
地的路径。下面的大部分内容是文档维护人员列出的最迫切需要完成的任务。

文档待办事项列表
----------------

为了使我们的文档达到应有的水平，需要完成的任务数不胜数。此列表包含许多重要的
项目，但还远远不够详尽；如果您知道改进文档的其他方法，请不要羞于启齿。

消除警告（WARNING）
~~~~~~~~~~~~~~~~~~~

文档构建目前出现了数量惊人的警告。虱子多了不痒，债多了不愁；大伙儿忽略了它们，
他们永远不会注意到他们的工作增加了新的警告。因此，消除警告是文档待办事项列表
中优先级最高的任务之一。这项任务本身相当简单，但必须以正确的方式进行，才能取
得成功。

C代码编译器发出的警告常常会被视为误报，从而导致出现了旨在让编译器闭嘴的补丁。
文档构建中的警告几乎总是指向真正的问题；要消除这些警告，需要理解问题并从源头上
解决问题。因此，修复文档警告的补丁不应在标题中直接写“修复警告”；它们应该指明
真正修复的问题。

另一个重点是，文档警告常常由C代码里kernel-doc注释中的问题引起。虽然文档维护
人员对收到这些修复补丁的副本表示感谢，但是文档树实际上通常并不适合接受这些
补丁；它们应该被交给相关子系统的维护人员。

例如，在一次文档构建中，我几乎是随意选取了一对警告::

  ./drivers/devfreq/devfreq.c:1818: warning: bad line:
  	- Resource-managed devfreq_register_notifier()
  ./drivers/devfreq/devfreq.c:1854: warning: bad line:
  	- Resource-managed devfreq_unregister_notifier()

（作了断行以便于阅读）

简单看一下上面给出的源文件，会发现几个kernel-doc注释，如下所示::

  /**
   * devm_devfreq_register_notifier()
   	- Resource-managed devfreq_register_notifier()
   * @dev:	The devfreq user device. (parent of devfreq)
   * @devfreq:	The devfreq object.
   * @nb:	The notifier block to be unregistered.
   * @list:	DEVFREQ_TRANSITION_NOTIFIER.
   */

问题在于缺了一个“*”，这不符合构建系统对C注释块的格式要求。此问题自2016年注释
被添加以来一直存在——整整四年之久。修复它只需要添加丢失的星号。看一眼该文件的
历史记录以了解主题行的常规格式是什么样，再使用 ``scripts/get_maintainer.pl``
来搞清谁应当收到此补丁。生成的补丁如下所示::

  [PATCH] PM / devfreq: Fix two malformed kerneldoc comments

  Two kerneldoc comments in devfreq.c fail to adhere to the required format,
  resulting in these doc-build warnings:

    ./drivers/devfreq/devfreq.c:1818: warning: bad line:
  	  - Resource-managed devfreq_register_notifier()
    ./drivers/devfreq/devfreq.c:1854: warning: bad line:
  	  - Resource-managed devfreq_unregister_notifier()

  Add a couple of missing asterisks and make kerneldoc a little happier.

  Signed-off-by: Jonathan Corbet <corbet@lwn.net>
  ---
   drivers/devfreq/devfreq.c | 4 ++--
   1 file changed, 2 insertions(+), 2 deletions(-)

  diff --git a/drivers/devfreq/devfreq.c b/drivers/devfreq/devfreq.c
  index 57f6944d65a6..00c9b80b3d33 100644
  --- a/drivers/devfreq/devfreq.c
  +++ b/drivers/devfreq/devfreq.c
  @@ -1814,7 +1814,7 @@ static void devm_devfreq_notifier_release(struct device *dev, void *res)

   /**
    * devm_devfreq_register_notifier()
  -	- Resource-managed devfreq_register_notifier()
  + *	- Resource-managed devfreq_register_notifier()
    * @dev:	The devfreq user device. (parent of devfreq)
    * @devfreq:	The devfreq object.
    * @nb:		The notifier block to be unregistered.
  @@ -1850,7 +1850,7 @@ EXPORT_SYMBOL(devm_devfreq_register_notifier);

   /**
    * devm_devfreq_unregister_notifier()
  -	- Resource-managed devfreq_unregister_notifier()
  + *	- Resource-managed devfreq_unregister_notifier()
    * @dev:	The devfreq user device. (parent of devfreq)
    * @devfreq:	The devfreq object.
    * @nb:		The notifier block to be unregistered.
  --
  2.24.1

整个过程只花了几分钟。当然，我后来发现有人在另一个树中修复了它，这亮出了另一
个教训：在深入研究问题之前，一定要检查linux-next树，看看问题是否已经修复。

其他修复可能需要更长的时间，尤其是那些与缺少文档的结构体成员或函数参数相关的
修复。这种情况下，需要找出这些成员或参数的作用，并正确描述它们。总之，这种
任务有时会有点乏味，但它非常重要。如果我们真的可以从文档构建中消除警告，那么
我们就可以开始期望开发人员开始注意避免添加新的警告了。

“迷失的”kernel-doc注释
~~~~~~~~~~~~~~~~~~~~~~

开发者被鼓励去为他们的代码写kernel-doc注释，但是许多注释从未被引入文档构建。
这使得这些信息更难找到，例如使Sphinx无法生成指向该文档的链接。将 ``kernel-doc``
指令添加到文档中以引入这些注释可以帮助社区获得为编写注释所做工作的全部价值。

``scripts/find-unused-docs.sh`` 工具可以用来找到这些被忽略的评论。

请注意，将导出的函数和数据结构引入文档是最有价值的。许多子系统还具有供内部
使用的kernel-doc注释；除非这些注释放在专门针对相关子系统开发人员的文档中，
否则不应将其引入文档构建中。


修正错字
~~~~~~~~


修复文档中的排版或格式错误是一种快速学习如何创建和发送修补程序的方法，也是
一项有用的服务。我总是愿意接受这样的补丁。这也意味着，一旦你修复了一些这种
错误，请考虑转移到更高级的任务，留下一些拼写错误给下一个初学者解决。

请注意，有些并 **不是** 拼写错误，不应该被“修复”：

 - 内核文档中用美式和英式英语拼写皆可，没有必要互相倒换。

 - 在内核文档中，没必要讨论句点后面应该跟一个还是两个空格的问题。其他一些有
   合理分歧的地方，比如“牛津逗号”，在这也是跑题的。

与对任何项目的任何补丁一样，请考虑您的更改是否真的让事情变得更好。

“上古”文档
~~~~~~~~~~

一些内核文档是最新的、被维护的，并且非常有用，有些文件确并非如此。尘封、陈旧
和不准确的文档可能会误导读者，并对我们的整个文档产生怀疑。任何解决这些问题的
事情都是非常受欢迎的。

无论何时处理文档，请考虑它是否是最新的，是否需要更新，或者是否应该完全删除。
您可以注意以下几个警告标志：

 - 对2.x内核的引用
 - 指向SourceForge存储库
 - 历史记录除了拼写错误啥也没有持续几年
 - 讨论Git之前时代的工作流

当然，最好的办法是更新文档，添加所需的任何信息。这样的工作通常需要与熟悉相关
子系统的开发人员合作。当有人善意地询问开发人员，并听取他们的回答然后采取
行动时，开发人员通常更愿意与这些致力于改进文档的人员合作。

有些文档已经没希望了；例如，我们偶尔会发现引用了很久以前从内核中删除的代码的
文档。删除过时的文档会碰见令人惊讶的阻力，但我们无论如何都应该这样做。文档中
多余的粗枝大叶对任何人都没有帮助。

如果一个严重过时的文档中可能有一些有用的信息，而您又无法更新它，那么最好在
开头添加一个警告。建议使用以下文本::

  .. warning ::
  	This document is outdated and in need of attention.  Please use
  	this information with caution, and please consider sending patches
  	to update it.

这样的话，至少我们长期受苦的读者会得到文件可能会把他们引入歧途的警告。

文档一致性
~~~~~~~~~~

这里的老前辈们会记得上世纪90年代出现在书架上的Linux书籍，它们只是从网上不同
位置搜来的文档文件的集合。在此之后，（大部分）这些书都得到了改进，但是内核的
文档仍然主要是建立在这种模型上。它有数千个文件，几乎每个文件都是与其他文件相
独立编写的。我们没有一个连贯的内核文档；只有数千个独立的文档。

我们一直试图通过编篡一套“书籍”来改善这种情况，以为特定读者提供成套文档。这
包括：

 - Documentation/translations/zh_CN/admin-guide/index.rst
 - Documentation/core-api/index.rst
 - Documentation/driver-api/index.rst
 - Documentation/userspace-api/index.rst

以及文档本身这本“书”。

将文档移到适当的书中是一项重要的任务，需要继续进行。不过这项工作还是有一些
挑战性。移动文档会给处理这些文档的人带来短期的痛苦；他们对这些更改无甚热情
也是可以理解的。通常情况下，可以将一个文档移动一下；不过我们真的不想一直移动
它们。

即使所有文件都在正确的位置，我们也只是把一大堆文件变成一群小堆文件。试图将
所有这些文件组合成一个整体的工作尚未开始。如果你对如何在这方面取得进展有好的
想法，我们将很高兴了解。

样式表（Stylesheet）改进
~~~~~~~~~~~~~~~~~~~~~~~~

随着Sphinx的采用，我们得到了比以前更好的HTML输出。但它仍然需要很大的改进；
Donald Knuth和Edward Tufte可能是映像平平的。这需要调整我们的样式表，以创建
更具排版效果、可访问性和可读性的输出。

请注意：如果你承担这个任务，你将进入经典的两难领域。即使是相对明显的变化，
也会有很多意见和讨论。唉，这就是我们生活的世界的本质。

无LaTeX的PDF构建
~~~~~~~~~~~~~~~~

对于拥有大量时间和Python技能的人来说，这绝对是一项不平凡的任务。Sphinx工具链
相对较小且包含良好；很容易添加到开发系统中。但是构建PDF或EPUB输出需要安装
LaTeX，它绝对称不上小或包含良好的。消除Latex将是一件很好的事情。

最初是希望使用 `rst2pdf <https://rst2pdf.org/>`_ 工具来生成PDF，但结果发现
无法胜任这项任务。不过rst2pdf的开发工作最近似乎又有了起色，这是个充满希望的
迹象。如果有开发人员愿意与该项目合作，使rst2pdf可与内核文档构建一起工作，
大家会非常感激。

编写更多文档
~~~~~~~~~~~~

当然，内核中许多部分的文档严重不足。如果您有编写一个特定内核子系统文档的相应
知识并愿意这样做，请不要犹豫，编写并向内核贡献结果吧！数不清的内核开发人员和
用户会感谢你。
