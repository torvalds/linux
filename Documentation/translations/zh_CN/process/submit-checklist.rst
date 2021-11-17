.. include:: ../disclaimer-zh_CN.rst

:Original: :ref:`Documentation/process/submit-checklist.rst <submitchecklist>`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>

.. _cn_submitchecklist:

Linux内核补丁提交清单
~~~~~~~~~~~~~~~~~~~~~

如果开发人员希望看到他们的内核补丁提交更快地被接受，那么他们应该做一些基本
的事情。

这些都是在
:ref:`Documentation/translations/zh_CN/process/submitting-patches.rst <cn_submittingpatches>`
和其他有关提交Linux内核补丁的文档中提供的。

1) 如果使用工具，则包括定义/声明该工具的文件。不要依赖于其他头文件拉入您使用
   的头文件。

2) 干净的编译：

   a) 使用适用或修改的 ``CONFIG`` 选项 ``=y``、``=m`` 和 ``=n`` 。没有GCC
      警告/错误，没有链接器警告/错误。

   b) 通过allnoconfig、allmodconfig

   c) 使用 ``O=builddir`` 时可以成功编译

3) 通过使用本地交叉编译工具或其他一些构建场在多个CPU体系结构上构建。

4) PPC64是一种很好的交叉编译检查体系结构，因为它倾向于对64位的数使用无符号
   长整型。

5) 如下所述 :ref:`Documentation/translations/zh_CN/process/coding-style.rst <cn_codingstyle>`.
   检查您的补丁是否为常规样式。在提交（ ``scripts/check patch.pl`` ）之前，
   使用补丁样式检查器检查是否有轻微的冲突。您应该能够处理您的补丁中存在的所有
   违规行为。

6) 任何新的或修改过的 ``CONFIG`` 选项都不会弄脏配置菜单，并默认为关闭，除非
   它们符合 ``Documentation/kbuild/kconfig-language.rst`` 中记录的异常条件,
   菜单属性：默认值.

7) 所有新的 ``kconfig`` 选项都有帮助文本。

8) 已仔细审查了相关的 ``Kconfig`` 组合。这很难用测试来纠正——脑力在这里是有
   回报的。

9) 用 sparse 检查干净。

10) 使用 ``make checkstack`` 和 ``make namespacecheck`` 并修复他们发现的任何
    问题。

    .. note::

        ``checkstack`` 并没有明确指出问题，但是任何一个在堆栈上使用超过512
        字节的函数都可以进行更改。

11) 包括 :ref:`kernel-doc <kernel_doc>` 内核文档以记录全局内核API。（静态函数
    不需要，但也可以。）使用 ``make htmldocs`` 或 ``make pdfdocs`` 检查
    :ref:`kernel-doc <kernel_doc>` 并修复任何问题。

12) 通过以下选项同时启用的测试 ``CONFIG_PREEMPT``, ``CONFIG_DEBUG_PREEMPT``,
    ``CONFIG_DEBUG_SLAB``, ``CONFIG_DEBUG_PAGEALLOC``, ``CONFIG_DEBUG_MUTEXES``,
    ``CONFIG_DEBUG_SPINLOCK``, ``CONFIG_DEBUG_ATOMIC_SLEEP``,
    ``CONFIG_PROVE_RCU`` and ``CONFIG_DEBUG_OBJECTS_RCU_HEAD``

13) 已经过构建和运行时测试，包括有或没有 ``CONFIG_SMP``, ``CONFIG_PREEMPT``.

14) 如果补丁程序影响IO/磁盘等：使用或不使用 ``CONFIG_LBDAF`` 进行测试。

15) 所有代码路径都已在启用所有lockdep功能的情况下运行。

16) 所有新的/proc条目都记录在 ``Documentation/``

17) 所有新的内核引导参数都记录在
    Documentation/admin-guide/kernel-parameters.rst 中。

18) 所有新的模块参数都记录在 ``MODULE_PARM_DESC()``

19) 所有新的用户空间接口都记录在 ``Documentation/ABI/`` 中。有关详细信息，
    请参阅 ``Documentation/ABI/README`` 。更改用户空间接口的补丁应该抄送
    linux-api@vger.kernel.org。

20) 已通过至少注入slab和page分配失败进行检查。请参阅 ``Documentation/fault-injection/``
    如果新代码是实质性的，那么添加子系统特定的故障注入可能是合适的。

21) 新添加的代码已经用 ``gcc -W`` 编译（使用 ``make EXTRA-CFLAGS=-W`` ）。这
    将产生大量噪声，但对于查找诸如“警告：有符号和无符号之间的比较”之类的错误
    很有用。

22) 在它被合并到-mm补丁集中之后进行测试，以确保它仍然与所有其他排队的补丁以
    及VM、VFS和其他子系统中的各种更改一起工作。

23) 所有内存屏障例如 ``barrier()``, ``rmb()``, ``wmb()`` 都需要源代码中的注
    释来解释它们正在执行的操作及其原因的逻辑。

24) 如果补丁添加了任何ioctl，那么也要更新 ``Documentation/userspace-api/ioctl/ioctl-number.rst``

25) 如果修改后的源代码依赖或使用与以下 ``Kconfig`` 符号相关的任何内核API或
    功能，则在禁用相关 ``Kconfig`` 符号和/或 ``=m`` （如果该选项可用）的情况
    下测试以下多个构建[并非所有这些都同时存在，只是它们的各种/随机组合]：

    ``CONFIG_SMP``, ``CONFIG_SYSFS``, ``CONFIG_PROC_FS``, ``CONFIG_INPUT``, ``CONFIG_PCI``, ``CONFIG_BLOCK``, ``CONFIG_PM``, ``CONFIG_MAGIC_SYSRQ``,
    ``CONFIG_NET``, ``CONFIG_INET=n`` (但是后者伴随 ``CONFIG_NET=y``).
