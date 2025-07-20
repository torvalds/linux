.. SPDX-License-Identifier: GPL-2.0

.. Translated by Akira Yokosawa <akiyks@gmail.com>

.. An old translation of this document of a different origin was at
   Documentation/translations/ja_JP/SubmitChecklist, which can be found
   in the pre-v6.14 tree if you are interested.
   Please note that this translation is independent of the previous one.

======================================
Linux カーネルパッチ投稿チェックリスト
======================================

.. note:: 【訳註】
   この文書は、
   Documentation/process/submit-checklist.rst
   の翻訳です。
   免責条項については、
   :ref:`免責条項の抄訳 <translations_ja_JP_disclaimer>` および、
   :ref:`Disclaimer (英語版) <translations_disclaimer>` を参照してください。

以下は、カーネルパッチの投稿時に、そのスムーズな受け入れのために心がける
べき基本的な事項です。

これは、 Documentation/process/submitting-patches.rst およびその他の
Linux カーネルパッチ投稿に関する文書を踏まえ、それを補足するものです。

.. note:: 【訳註】
   可能な項目については、パッチもしくはパッチ内の更新を暗黙の主語として、
   その望ましい状態を表す文体とします。その他、原義を損なわない範囲で
   係り結びを調整するなど、簡潔で把握しやすい箇条書きを目指します。


コードのレビュー
================

1) 利用する機能について、その機能を定義・宣言しているファイルを
   ``#include`` している。
   他のヘッダーファイル経由での取り込みに依存しない。

2) Documentation/process/coding-style.rst に詳述されている一般的なスタイル
   についてチェック済み。

3) メモリバリアー (例, ``barrier()``, ``rmb()``, ``wmb()``) について、
   そのすべてに、作用と目的、及び必要理由についての説明がソースコード内の
   コメントとして記述されている。


Kconfig 変更のレビュー
======================

1) 新規の、もしくは変更された ``CONFIG`` オプションについて、それが関係する
   コンフィグメニューへの悪影響がない。また、
   Documentation/kbuild/kconfig-language.rst の
   "Menu attibutes: default value" に記載の例外条件を満たす場合を除き、
   そのデフォルトが無効になっている。

2) 新規の ``Kconfig`` オプションにヘルプテキストがある。

3) 妥当な ``Kconfig`` の組み合わせについて注意深くレビューされている。
   これをテストでやり切るのは困難で、知力が決め手となる。

ドキュメンテーションの作成
==========================

1) グローバルなカーネル API が  :ref:`kernel-doc <kernel_doc>` の形式で
   ドキュメント化されている (静的関数には求められないが、付けてもよい)。

2) 新規 ``/proc`` エントリーが、すべて ``Documentation/`` 以下に記載されて
   いる。

3) 新規カーネル・ブート・パラメータが、すべて
   ``Documentation/admin-guide/kernel-parameters.rst`` に記載されている。

4) 新規モジュール・パラメータが、すべて ``MODULE_PARM_DESC()`` によって記述
   されている。

5) 新規ユーザースペース・インターフェースが、すべて ``Documentaion/ABI/``
   以下に記載されている。詳しくは、 Documentation/admin-guide/abi.rst
   (もしくは ``Documentation/ABI/README``) を参照。
   ユーザースペース・インターフェースを変更するパッチは、
   linux-api@vger.kernel.org にも CC すべし。

6) なんらかの ioctl を追加するパッチは、
   ``Documentation/userspace-api/ioctl/ioctl-number.rst``
   の更新を伴う。

ツールによるコードのチェック
============================

1) スタイル・チェッカー (``scripts/checkpatch.pl``) によって、犯しがちな
   パッチ・スタイルの違反がないことを確認済み。
   指摘される違反を残す場合は、それを正当化できること。

2) sparse により入念にチェック済み。

3) ``make checkstack`` で指摘される問題があれば、それが修正済み。
   ``checkstack`` は問題点を明示的には指摘しないが、 スタック消費が
   512 バイトを越える関数は見直しの候補。

コードのビルド
==============

1) 以下の条件でクリーンにビルドできる。

   a) 適用可能な、および ``=y``, ``=m``, ``=n`` を変更した ``CONFIG``
      オプションでのビルド。
      ``gcc`` およびリンカーからの警告・エラーがないこと。

   b) ``allnoconfig`` と ``allmodconfig`` がパス

   c) ``O=builddir`` を指定してのビルド

   d) Documentation/ 以下の変更に関して、ドキュメントのビルドで新たな警告や
      エラーが出ない。
      ``make htmldocs`` または ``make pdfdocs`` でビルドし、問題があれば修正。

2) ローカルのクロス・コンパイル・ツール、その他のビルド環境 (訳註: build farm)
   を使って、複数の CPU アーキテクチャ向けにビルドできる。
   特に、ワードサイズ (32 ビットと 64 ビット) やエンディアン (ビッグとリトル)
   の異なるアーキテクチャを対象とするテストは、表現可能数値範囲・データ整列・
   エンディアンなどについての誤った仮定に起因する様々な移植上の問題を捕える
   のに効果的。

3) 新規に追加されたコードについて (``make KCFLAGS=-W`` を使って)
   ``gcc -W`` でコンパイル。
   これは多くのノイズを伴うが、
   ``warning: comparison between signed and unsigned``
   の類いのバグをあぶり出すのに効果的。

4) 変更されるソースコードが、下記の ``Kconfig`` シンボルに関連するカーネル
   API や機能に依存 (もしくは利用) する場合、それらの ``Kconfig`` シンボルが、
   無効、および (可能なら) ``=m`` の場合を組み合わせた複数のビルドを
   (全部まとめてではなく、いろいろなランダムの組み合わせで) テスト済み。

   ``CONFIG_SMP``, ``CONFIG_SYSFS``, ``CONFIG_PROC_FS``, ``CONFIG_INPUT``,
   ``CONFIG_PCI``, ``CONFIG_BLOCK``, ``CONFIG_PM``, ``CONFIG_MAGIC_SYSRQ``,
   ``CONFIG_NET``, ``CONFIG_INET=n`` (ただし、後者は ``CONFIG_NET=y``
   との組み合わせ)。

コードのテスト
==============

1) ``CONFIG_PREEMPT``, ``CONFIG_DEBUG_PREEMPT``,
   ``CONFIG_SLUB_DEBUG``, ``CONFIG_DEBUG_PAGEALLOC``, ``CONFIG_DEBUG_MUTEXES``,
   ``CONFIG_DEBUG_SPINLOCK``, ``CONFIG_DEBUG_ATOMIC_SLEEP``,
   ``CONFIG_PROVE_RCU`` および ``CONFIG_DEBUG_OBJECTS_RCU_HEAD`` をすべて
   同時に有効にしてのテスト済み。

2) ``CONFIG_SMP`` と ``CONFIG_PREEMPT`` が有効と無効の場合について、ビルドと
   ランタイムのテスト済み。

3) lockdep の機能をすべて有効にしての実行で、すべてのコード経路が確認済み。

4) 最低限、slab と ページ・アロケーションの失敗に関する誤り注入
   (訳註: fault injection) によるチェック済み。
   詳しくは、 Documentation/fault-injection/index.rst を参照。
   新規のコードが多い場合は、サブシステム対象の誤り注入を追加するのが望ましい
   可能性あり。

5) linux-next の最新タグに対するテストにより、他でキューイングされている
   パッチや、VM、VFS、その他のサブシステム内のすべての変更と組み合わせての
   動作を確認済み。
