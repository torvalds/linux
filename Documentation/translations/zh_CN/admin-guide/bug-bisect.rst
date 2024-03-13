.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../admin-guide/bug-bisect`

:译者:

 吴想成 Wu XiangCheng <bobwxc@email.cn>

二分（bisect）缺陷
+++++++++++++++++++

（英文版）最后更新：2016年10月28日

引言
=====

始终尝试由来自kernel.org的源代码构建的最新内核。如果您没有信心这样做，请将
错误报告给您的发行版供应商，而不是内核开发人员。

找到缺陷（bug）并不总是那么容易，不过仍然得去找。如果你找不到它，不要放弃。
尽可能多的向相关维护人员报告您发现的信息。请参阅MAINTAINERS文件以了解您所
关注的子系统的维护人员。

在提交错误报告之前，请阅读“Documentation/admin-guide/reporting-issues.rst”。

设备未出现（Devices not appearing）
====================================

这通常是由udev/systemd引起的。在将其归咎于内核之前先检查一下。

查找导致缺陷的补丁
===================

使用 ``git`` 提供的工具可以很容易地找到缺陷，只要缺陷是可复现的。

操作步骤：

- 从git源代码构建内核
- 以此开始二分 [#f1]_::

	$ git bisect start

- 标记损坏的变更集::

	$ git bisect bad [commit]

- 标记正常工作的变更集::

	$ git bisect good [commit]

- 重新构建内核并测试
- 使用以下任一与git bisect进行交互::

	$ git bisect good

  或::

	$ git bisect bad

  这取决于您测试的变更集上是否有缺陷
- 在一些交互之后，git bisect将给出可能导致缺陷的变更集。

- 例如，如果您知道当前版本有问题，而4.8版本是正常的，则可以执行以下操作::

	$ git bisect start
	$ git bisect bad                 # Current version is bad
	$ git bisect good v4.8


.. [#f1] 您可以（可选地）在开始git bisect的时候提供good或bad参数
         ``git bisect start [BAD] [GOOD]``

如需进一步参考，请阅读：

- ``git-bisect`` 的手册页
- `Fighting regressions with git bisect（用git bisect解决回归）
  <https://www.kernel.org/pub/software/scm/git/docs/git-bisect-lk2009.html>`_
- `Fully automated bisecting with "git bisect run"（使用git bisect run
  来全自动二分） <https://lwn.net/Articles/317154>`_
- `Using Git bisect to figure out when brokenness was introduced
  （使用Git二分来找出何时引入了错误） <http://webchick.net/node/99>`_
