.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :doc:`../../../admin-guide/bug-bisect`

:譯者:

 吳想成 Wu XiangCheng <bobwxc@email.cn>
 胡皓文 Hu Haowen <2023002089@link.tyut.edu.cn>

二分（bisect）缺陷
+++++++++++++++++++

（英文版）最後更新：2016年10月28日

引言
=====

始終嘗試由來自kernel.org的源代碼構建的最新內核。如果您沒有信心這樣做，請將
錯誤報告給您的發行版供應商，而不是內核開發人員。

找到缺陷（bug）並不總是那麼容易，不過仍然得去找。如果你找不到它，不要放棄。
儘可能多的向相關維護人員報告您發現的信息。請參閱MAINTAINERS文件以瞭解您所
關注的子系統的維護人員。

在提交錯誤報告之前，請閱讀“Documentation/admin-guide/reporting-issues.rst”。

設備未出現（Devices not appearing）
====================================

這通常是由udev/systemd引起的。在將其歸咎於內核之前先檢查一下。

查找導致缺陷的補丁
===================

使用 ``git`` 提供的工具可以很容易地找到缺陷，只要缺陷是可復現的。

操作步驟：

- 從git源代碼構建內核
- 以此開始二分 [#f1]_::

	$ git bisect start

- 標記損壞的變更集::

	$ git bisect bad [commit]

- 標記正常工作的變更集::

	$ git bisect good [commit]

- 重新構建內核並測試
- 使用以下任一與git bisect進行交互::

	$ git bisect good

  或::

	$ git bisect bad

  這取決於您測試的變更集上是否有缺陷
- 在一些交互之後，git bisect將給出可能導致缺陷的變更集。

- 例如，如果您知道當前版本有問題，而4.8版本是正常的，則可以執行以下操作::

	$ git bisect start
	$ git bisect bad                 # Current version is bad
	$ git bisect good v4.8


.. [#f1] 您可以（可選地）在開始git bisect的時候提供good或bad參數
         ``git bisect start [BAD] [GOOD]``

如需進一步參考，請閱讀：

- ``git-bisect`` 的手冊頁
- `Fighting regressions with git bisect（用git bisect解決迴歸）
  <https://www.kernel.org/pub/software/scm/git/docs/git-bisect-lk2009.html>`_
- `Fully automated bisecting with "git bisect run"（使用git bisect run
  來全自動二分） <https://lwn.net/Articles/317154>`_
- `Using Git bisect to figure out when brokenness was introduced
  （使用Git二分來找出何時引入了錯誤） <http://webchick.net/node/99>`_

