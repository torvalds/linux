.. _jp_process_submitting_patches:

パッチの投稿: カーネルにコードを入れるための必須ガイド
======================================================

.. note::

   このドキュメントは :ref:`Documentation/process/submitting-patches.rst <submittingpatches>` の日本語訳です。

   免責事項: :ref:`translations_ja_JP_disclaimer`

.. warning::

   **UNDER CONSTRUCTION!!**

   この文書は翻訳更新の作業中です。最新の内容は原文を参照してください。

Linux カーネルへ変更を投稿したい個人や企業にとって、もし「仕組み」に
慣れていなければ、そのプロセスは時に気後れするものでしょう。
このテキストは、あなたの変更が受け入れられる可能性を大きく高めるための
提案を集めたものです。

この文書には、比較的簡潔な形式で多数の提案が含まれています。
カーネル開発プロセスの仕組みに関する詳細は
Documentation/process/development-process.rst を参照してください。
また、コードを投稿する前に確認すべき項目の一覧として
Documentation/process/submit-checklist.rst を読んでください。
デバイスツリーバインディングのパッチについては、
Documentation/devicetree/bindings/submitting-patches.rst を読んでください。

この文書は、パッチ作成に ``git`` を使う前提で書かれています。
もし ``git`` に不慣れであれば、使い方を学ぶことを強く勧めます。
それにより、カーネル開発者として、また一般的にも、あなたの作業は
ずっと楽になるでしょう。

いくつかのサブシステムやメンテナツリーには、各々のワークフローや
期待事項に関する追加情報があります。次を参照してください:
:ref:`Documentation/process/maintainer-handbooks.rst <maintainer_handbooks_main>`.

現在のソースツリーを入手する
----------------------------

もし手元に最新のカーネルソースのリポジトリがなければ、``git`` を使って取得して
ください。まずは mainline のリポジトリから始めるのがよいでしょう。これは
次のようにして取得できます::

  git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

ただし、直接 mainline のツリーを対象に作業すればよいとは限らないことに注意
してください。多くのサブシステムのメンテナはそれぞれ独自のツリーを運用しており、
そのツリーに対して作成されたパッチを見たいと考えています。該当サブシステムの
ツリーは MAINTAINERS ファイル内の **T:** エントリを参照して見つけてください。
そこに掲載されていない場合は、メンテナに問い合わせてください。

変更内容を説明する
------------------
