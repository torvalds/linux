.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/kbuild/reproducible-builds.rst

:Translator: 慕冬亮 Dongliang Mu <dzm91@hust.edu.cn>

============
可重现的构建
============

通常希望使用相同工具集构建相同源代码是可重现的，即，输出始终完全相同。这使得能够验证
二进制分发或嵌入式系统的构建基础设施未被篡改。这样也更容易验证源代码或工具的更改不会
影响最终生成的二进制文件。

`可重现构建项目`_ 提供了有关该主题的更多信息。本文档涵盖了构建内核可能不可重现的
各种原因，以及如何避免这些问题。

时间戳
------

内核在三个地方嵌入时间戳：

* 通过 ``uname()`` 显示与包含在 ``/proc/version`` 中的版本字符串

* initramfs 中的文件时间戳

* 如果启动 ``CONFIG_IKHEADERS``，内核或相应模块中嵌入的内核头文件的时间戳，
  通过 ``/sys/kernel/kheaders.tar.xz`` 显示

默认情况下，时间戳为当前时间或内核头文件的修改时间。这个内容必须使用
`KBUILD_BUILD_TIMESTAMP`_ 变量进行覆盖。如果你从某个 git 提交进行构建，
可以使用其提交日期。

内核 *不* 使用 ``__DATE__`` 和 ``__TIME__`` 宏，并在使用这些宏时启用警告。
如果你合并的外部代码使用这些宏，则必须通过设置 `SOURCE_DATE_EPOCH`_ 环境
变量来覆盖它们对应的时间戳。

用户，主机
----------

内核在 ``/proc/version`` 中嵌入构建用户和主机名。必须使用
`KBUILD_BUILD_USER 和 KBUILD_BUILD_HOST`_ 变量来覆盖这些设置。如果
您从某个 git 提交进行构建，可以使用其提交者地址。

绝对文件名
----------

当内核在树外构建时，调试信息可能包括源文件的绝对文件名。这些信息必须通过在
`KCFLAGS`_ 变量中包含 ``-fdebug-prefix-map`` 选项来覆盖。

根据使用的编译器，``__FILE__`` 宏在树外构建中也可能扩展为绝对文件名。Kbuild
自动使用 ``-fmacro-prefix-map`` 选项来防止这种情况，前提是它被支持。

可重现构建网站提供了有关这些 `prefix-map 选项`_ 的更多信息。

在源包中的生成文件
------------------

在 ``tools/`` 子目录下，一些程序的构建过程并不完全支持树外构建。这可能导致后续
使用如 ``make rpm-pkg`` 构建的源码包包含生成的文件。在构建源码包之前，您应该通过
运行 ``make mrproper`` 或 ``git clean -d -f -x`` 来确保源码树是干净的。

模块签名
--------

如果你启用 ``CONFIG_MODULE_SIG_ALL``，默认行为是为每次构建生成不同的临时密钥，
从而导致模块不可重现。然而，将签名密钥包含在源代码中显然会违背签名模块的目的。

一种方法是将构建过程分为几个部分，以便不可重现的部分可以作为源处理：

1. 生成一个持久的签名密钥。将该密钥的证书添加到内核源代码中。

2. 将 ``CONFIG_SYSTEM_TRUSTED_KEYS`` 符号设置为包括签名密钥的证书，将
``CONFIG_MODULE_SIG_KEY`` 设置为空字符串，并禁用 ``CONFIG_MODULE_SIG_ALL``。
最后，构建内核和模块。

3. 为模块创建分离的签名，并将它们作为源发布。

4. 附加模块签名并进行第二次构建。这可以重建模块，或使用步骤 2 的输出。

结构随机化
----------

如果你启用 ``CONFIG_RANDSTRUCT``，则需要在 ``scripts/basic/randstruct.seed``
中预生成随机种子，以便每次构建都使用相同的值。有关详细信息，请参见
``scripts/gen-randstruct-seed.sh``。

调试信息冲突
------------

这并非是个不可重现性的问题，而是生成的文件 *过于* 可重现的问题。

一旦你设置了所有必要的变量来开展可重现构建，vDSO 的调试信息可能即使对于不同的内核版
本也是相同的。这会导致不同内核版本的调试信息软件包之间发生文件冲突。

为了避免这种情况，你可以通过在 vDSO 中包含一个任意的 salt 字符串，使其对于不同的
内核版本是不同的。这种机制由 Kconfig 符号 ``CONFIG_BUILD_SALT`` 指定。

Git
---

未提交的更改或 Git 中的不同提交 ID 也可能导致不同的编译结果。例如，在执行
``git reset HEAD^`` 后，即使代码相同，编译期间生成的
``include/config/kernel.release`` 也会不同，导致最终生成的二进制文件也不尽相同。
有关详细信息，请参见 ``scripts/setlocalversion``。

.. _KBUILD_BUILD_TIMESTAMP: kbuild.html#kbuild-build-timestamp
.. _KBUILD_BUILD_USER 和 KBUILD_BUILD_HOST: kbuild.html#kbuild-build-user-kbuild-build-host
.. _KCFLAGS: kbuild.html#kcflags
.. _prefix-map 选项: https://reproducible-builds.org/docs/build-path/
.. _可重现构建项目: https://reproducible-builds.org/
.. _SOURCE_DATE_EPOCH: https://reproducible-builds.org/docs/source-date-epoch/
