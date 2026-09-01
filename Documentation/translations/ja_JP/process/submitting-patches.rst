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
Documentation/process/maintainer-handbooks.rst.

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

変更内容を記述する
------------------

まず問題点を記べてください。あなたのパッチが 1 行のバグ修正であっても、
5000 行の新機能であっても、それを行う動機となった根本的な問題が
必ずあるはずです。レビューアが、修正すべき問題がたしかに存在し、冒頭の
段落の続きを読むべきだと納得できるように書いてください。

次にユーザーから見える影響を記述してください。クラッシュやロックアップは
分かりやすいですが、すべてのバグがそこまで露骨とは限りません。
たとえコードレビュー中に見つかった問題であっても、ユーザーに
どのような影響があり得るかを記述してください。
Linux の多くの環境は、上流から特定のパッチだけを取り込む二次的な
安定版ツリーや、ベンダー／製品固有のツリーのカーネルで動いています。
したがって、変更を適切に下流へ流す助けになる情報（発生条件、dmesg
の抜粋、クラッシュ内容、性能劣化、レイテンシのスパイク、
ロックアップ等）があれば記載してください。

次に最適化とトレードオフを定量的に示してください。パフォーマンス、
メモリ消費量、スタックフットプリント、バイナリサイズの改善を主張する
場合は、それを裏付ける数値を記載してください。
また、目に見えないコストについても記述してください。多くの場合、
最適化は CPU・メモリ・可読性の間でのトレードオフとなります。
ヒューリスティクスの場合は、異なるワークロード間でのトレードオフと
なります。レビューアがコストとメリットを比較検討できるよう、
最適化に伴って想定されるデメリットも記述してください。

問題点の明確化が済んだら、実際にどのような対策を講じているかを技術的に
詳しく説明してください。コードが意図したとおりに動作していることを
レビューアが確認できるよう、変更内容を平易な言葉で書き下すことが重要です。

パッチ説明を Linux のソースコード管理システム ``git`` の
「コミットログ」としてそのまま取り込める形で書けば、メンテナは
助かります。詳細は原文の該当節 ("The canonical patch format") を
参照してください。

.. TODO: Convert to file-local cross-reference when the destination is
   translated.

1 つのパッチでは 1 つの問題だけを解決してください。記述が長くなり
始めたら、パッチを分割すべきサインです。詳細は `変更を分割する`_ を
参照してください。


パッチまたはパッチシリーズを投稿／再投稿する際は、その完全な
説明と、それを正当化する理由を含めてください。単に「これはパッチ
（シリーズ）のバージョン N です」とだけ書くのは避けてください。
サブシステムメンテナが以前のパッチバージョンや参照先 URL をさかのぼって
パッチ記述を探し、それをパッチに補うことを期待してはいけません。
つまり、パッチ（シリーズ）とその説明は、それだけで完結しているべき
です。これはメンテナとレビューアの双方に有益です。レビューアの
中には、以前のパッチバージョンを受け取っていない人もいるでしょう。

変更内容は、あたかもコードベースに対してその振る舞いを変えるように
命令するかの如く、（訳補: 英語の）命令形で記述してください。たとえば、
"[This patch] makes xyzzy do frotz" や
"[I] changed xyzzy to do frotz" のような言い回しを避け、
"make xyzzy do frotz" のように書いてください。

特定のコミットに言及したい場合に、コミットの SHA-1 ID だけを
書くのは避けてください。レビューアがそれが何についてのものかを
把握しやすいよう、コミットの 1 行要約も含めてください。例::

	Commit e21d2170f36602ae2708 ("video: remove unnecessary
	platform_set_drvdata()") removed the unnecessary
	platform_set_drvdata(), but left the variable "dev" unused,
	delete it.

また、SHA-1 ID は少なくとも先頭 12 文字を使うようにしてください。
カーネルのリポジトリには\ **非常に多くの**\ オブジェクトがあるため、
それより短い ID では衝突が現実問題となります。6 文字の ID が今現在
衝突しないからといって、5 年後もそうであるとは限らないことを念頭に
置いてください。

変更に関連する議論や、その背景情報が Web 上で参照できる場合は、
それを指す 'Link:' タグを追加してください。過去のメーリングリスト
での議論や、Web に記録された何かに由来するパッチならば、
それを示してください。

メーリングリストのアーカイブへリンクする場合は、できれば lore.kernel.org
のメッセージアーカイブサービスを使ってください。リンク URL を作るには、
そのメッセージの ``Message-ID`` ヘッダの内容から、前後の山括弧を取り除いた
ものを使います。例::

    Link: https://lore.kernel.org/30th.anniversary.repost@klaava.Helsinki.FI

実際にリンクが機能し、該当するメッセージを指していることを
確認してください。

ただし、外部リソースを見なくても説明が理解できるようにするよう努めてください。
メーリングリストのアーカイブやバグへの URL を示すだけでなく、
投稿されたパッチに至った議論のポイントも要約してください。

パッチがバグを修正するものであれば、メーリングリストのアーカイブや
公開バグトラッカー上の報告を指す URL を付けて、``Closes:`` タグを
使ってください。例::

    Closes: https://example.com/issues/1234

このようなタグ付きのコミットが適用されたとき、自動的に issue を
閉じるバグトラッカーもあります。メーリングリストを監視している
ボットの中には、そのようなタグを追跡して一定の動作を行うものも
あります。ただし、非公開バグトラッカーの（訳補: 部外者が）閲覧できない
URL は禁止です。

パッチが特定のコミットに含まれるバグを修正するもの、たとえば
``git bisect`` で問題を見つけたものの場合には、SHA-1 ID の
先頭少なくとも 12 文字と 1 行要約を含めて 'Fixes:' タグを
使ってください。タグを複数行に分割してはいけません。タグは
解析スクリプトを単純にするため、「75 桁で折り返す」規則の
例外です。例::

    Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

``git log`` や ``git show`` の出力を上の形式で整形させるには、
次の ``git config`` 設定が使えます::

    [core]
        abbrev = 12
    [pretty]
        fixes = Fixes: %h ("%s")

呼び出し例::

    $ git log -1 --pretty=fixes 54a4f0239f2e
    Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

変更を分割する
--------------

それぞれの\ **論理的な変更**\ は、個別のパッチに分けてください。

たとえば、単一のドライバに対する変更にバグ修正と性能改善の
両方が含まれるなら、それらは 2 つ以上のパッチに分けてください。
変更に API の更新と、その新しい API を使う新しいドライバが
含まれるなら、それらは 2 つのパッチに分けてください。

一方、多数のファイルに対して単一の変更を行う場合は、それらを
1 つのパッチにまとめてください。つまり、1 つの論理的な変更は
1 つのパッチに含めるべきです。

覚えておくべき点は、各パッチがレビューアに理解しやすく、
検証できる変更であるべきだということです。各パッチは、
それ自体の妥当性で正当化できなければなりません。

変更を完成させるために、あるパッチが別のパッチに依存するなら、
それでも構いません。単に、パッチの説明に
**"this patch depends on patch X"** と記してください。

変更を一連のパッチに分ける際は、シリーズ中の各パッチを
適用した後でも、カーネルが正しくビルドされ、正常に動作することを
特に注意して確認してください。問題の追跡に ``git bisect`` を
使う開発者は、あなたのパッチシリーズを任意の地点で分割する
ことがあります。途中でバグを持ち込めば、彼らに感謝されることは
ないでしょう。

パッチセットをそれ以上小さくできないなら、一度に投稿するのは
15 個程度までにして、レビューと統合を待ってください。


変更のスタイルを確認する
------------------------

パッチに基本的なスタイル違反がないか確認してください。詳細は
Documentation/process/coding-style.rst を参照してください。
これを怠ると、単にレビューアの時間を無駄にするだけでなく、
パッチはおそらく読まれもせずに却下されます。

一つの重要な例外は、コードをあるファイルから別のファイルへ移動する場合です。
その際は、コードを移動するその同じパッチの中で、一切コードを変更しては
いけません。これにより、コードの移動という行為と、コードの変更とが
明確に区別されます。これは実際の差分のレビューを大いに助け、また、ツールを
使ったコード変更の履歴の追跡を容易にします。

提出前に、パッチスタイルチェッカー
(``scripts/checkpatch.pl``) でパッチを確認してください。
ただし、スタイルチェッカーは指針にすぎず、
人間の判断に取って代わるものではないことに注意してください。
違反が指摘されるままのコードの方が見栄えがよいなら、おそらく
そのままにしておくのが最善でしょう。

チェッカーは 3 つのレベルで報告します:

 - ERROR: 間違っている可能性が非常に高いもの
 - WARNING: 慎重なレビューを要するもの
 - CHECK: 検討を要するもの

パッチに違反を残す場合は、そのすべてを正当化できなければなりません。


パッチの宛先を選択する
----------------------

各パッチでは、そのコードを保守する適切なサブシステムメンテナと
メーリングリストを、必ず Cc に入れてください。誰がその
メンテナかは、MAINTAINERS ファイルとソースコードの改訂履歴を
調べて確認してください。この段階では ``scripts/get_maintainer.pl``
が非常に役立ちます（パッチへのパスを引数として
``scripts/get_maintainer.pl`` に渡してください）。作業中の
サブシステムのメンテナが見つからない場合は、Andrew Morton
(akpm@linux-foundation.org) が最後の手段となるメンテナです。

すべてのパッチは、デフォルトで linux-kernel@vger.kernel.org にも
送られるべきですが、このリストは流量が多く、目を通さなくなった
開発者も少なくありません。とはいえ、無関係なメーリングリストや
無関係な人々にスパムを送らないでください。

カーネル関連のメーリングリストの多くは kernel.org で運営されており、
その一覧は https://subspace.kernel.org で確認できます。ただし、
他所で運営されているカーネル関連のメーリングリストもあります。

Linux カーネルに採用されるすべての変更の最終的な裁定者は
Linus Torvalds です。彼のメールアドレスは
<torvalds@linux-foundation.org> です。Linus は大量のメールを
受け取っており、現時点では直接彼を経由するパッチはごくわずかなので、
通常は彼にメールを送ることを極力\ **避けて**\ ください。

悪用可能なセキュリティバグを修正するパッチの場合は、
それを security@kernel.org に送ってください。深刻なバグに
ついては、ディストリビュータがユーザーにパッチを配布できるよう、
短期間の秘匿措置 (訳註: embargo) が検討される可能性があります。
ですので、その種のパッチを公開メーリングリストに送らないでください。
Documentation/process/security-bugs.rst も参照してください。

リリース済みカーネルの深刻なバグを修正するパッチは、次のような行を
パッチの sign-off 欄に入れることで、stable メンテナに知らせてください。
(メールの宛先ではないことに注意。) ::

  Cc: stable@vger.kernel.org

また、この文書に加えて Documentation/process/stable-kernel-rules.rst
も読んでください。

変更がユーザーランドとカーネルのインターフェースに影響する場合は、
MAINTAINERS ファイルに記載されている MAN-PAGES メンテナに
マニュアルページのパッチ、もしくは少なくとも変更の通知を送って、情報が
そちらにも反映されるようにしてください。ユーザー空間 API の
変更は、linux-api@vger.kernel.org にも Cc してください。

MIME・リンク・圧縮・添付なし、プレーンテキストのみ
----------------------------------------------------

Linus や他のカーネル開発者は、あなたが投稿する変更を読み、
コメントできる必要があります。カーネル開発者にとって、コードの特定の
箇所について、標準的なメールツールを使ってあなたの変更を「引用」し、
コメントできることが重要です。

このため、すべてのパッチはメール本文中に「インライン」で投稿すべきです。
これを行う最も簡単な方法は ``git send-email`` を使うことであり、
強く推奨されます。``git send-email`` の対話型チュートリアルは
https://git-send-email.io にあります。

``git send-email`` を使わない場合:

.. warning::

  パッチをコピー＆ペーストする際に、エディタによる自動改行で
  パッチが壊されないよう注意してください。

圧縮の有無にかかわらず、パッチを MIME 添付ファイルとして添付しては
いけません。よく使われるメールアプリケーションの多くは、MIME 添付
ファイルをプレーンテキストとして送信するとは限らず、あなたのコードに
対するコメントを妨げます。MIME 添付ファイルは Linus (訳補: をはじめ
とする開発者）が処理するのに余分な手間がかかるため、MIME 添付すると
その変更が受け入れられる可能性を下げることになります。

例外:  パッチがメーラーによって壊されている場合に、MIME による再送
を求められることがあります。

改変なしにパッチを送信するためのメールクライアント設定のヒントは、
Documentation/process/email-clients.rst を参照してください。


レビューコメントに応答する
--------------------------

あなたのパッチには、ほぼ確実に、その改善に向けてレビューアから
コメントが付きます。それは、あなたのメールへの返信という
形で届きます。それらのコメントには必ず応答してください。レビューアを
無視することは、あなたが無視されることにつながります。コメントに
答えるには、単にそのメールへ返信すればよいです。コード変更に
つながらないレビューコメントや質問であっても、次のレビューアが状況を
よりよく理解できるよう、多くの場合、コメントまたは changelog エントリ
として残すべきです。

どのような変更を行うのかを忘れずにレビューアに伝えてください。そして
時間を割いてくれることへの感謝を忘れないでください。
コードレビューは疲れる、時間のかかる作業であり、
ときにはレビューアが機嫌を損ねることもあります。そのような場合でも、
丁寧に応答し、指摘された問題に対応してください。次の版を送る際には、
カバーレターまたは個々のパッチに ``patch changelog`` を追加し、前回の
投稿との差分を説明してください。詳細は原文の該当節
("The canonical patch format") を参照してください。

.. TODO: Convert to file-local cross-reference when the destination is
   translated.

あなたのパッチにコメントしてくれた人たちは、パッチの Cc リストに追加して
新しい版について知らせてください。

メールクライアントとメーリングリストでの作法についての推奨事項は、
Documentation/process/email-clients.rst を参照してください。

要点に絞ったインライン返信での議論
---------------------------------------

Linux カーネル開発の議論では、全文引用 (訳註: top-posting) は強く非推奨です。
インライン返信 (訳註: interleaved reples or "inline" replies) を使うと、
会話の流れをずっと追いやすくなります。詳細は次を参照してください:
https://en.wikipedia.org/wiki/Posting_style#Interleaved_style

これについて、メーリングリストでは、次の引用をしばしば目にします::

  A: http://en.wikipedia.org/wiki/Top_post
  Q: Where do I find info about this thing called top-posting?
  A: Because it messes up the order in which people normally read text.
  Q: Why is top-posting such a bad thing?
  A: Top-posting.
  Q: What is the most annoying thing in e-mail?

同様に、返信に関係のない不要な引用はすべて削ってください。
そうすることで、返答を見つけやすくなり、時間と容量を節約できます。
詳細は次を参照してください: http://daringfireball.net/2007/07/on_top ::

  A: No.
  Q: Should I include quotations after my reply?


落胆しない - いらいらしない
---------------------------

変更を投稿した後は、辛抱強く待ってください。レビューアは忙しい人たちであり、
あなたのパッチにすぐに取りかかれるとは限りません。

かつては、パッチが何のコメントもなく虚空へ消えていくこともありましたが、
現在の開発プロセスはそれよりも円滑に機能しています。数週間以内、
通常は 2〜3 週間以内にコメントを受け取るはずです。そうならない場合は、
パッチを正しい場所へ送ったか確認してください。再投稿したりレビューアに
ping したりする前に、少なくとも 1 週間は待ってください。マージ期間
(訳註: merge window) のような忙しい時期には、さらに長く待ちましょう。

数週間後に、件名に "RESEND" を追加したパッチまたはパッチシリーズを
再送することは構いません::

   [PATCH Vx RESEND] sub/sys: Condensed patch summary

ただし、パッチまたはパッチシリーズの修正版を投稿する際には "RESEND"
を追加しないでください。
"RESEND" は、前回の投稿から一切変更のないパッチまたはパッチシリーズの
再送だけに当てはまります。


件名に PATCH を含める
---------------------

Linus と linux-kernel メーリングリストには大量のメールが届くため、
件名の先頭に ``[PATCH]`` を付けることが一般的な慣例となっています。
これにより、Linus や他のカーネル開発者は、パッチとその他の議論を
容易に区別できます。

``git send-email`` は、この指定を自動的に行います。


作業への署名 - Developer's Certificate of Origin
--------------------------------------------------

誰が何を行ったのかを追跡しやすくするため、特にパッチが複数階層の
メンテナーを経由して最終的にカーネルへ取り込まれる場合に備えて、
メールでやり取りされるパッチには sign-off の手続きが導入されています。

sign-off は、パッチの説明の末尾に追加する単純な一行です。これは、
そのパッチを自分で作成したか、オープンソースのパッチとして提出する
権利を持っていることを証明します。

.. note:: 【訳註】

   ``Signed-off-by`` によって同意する対象は、翻訳文ではなく、
   以下に示す英語原文の Developer's Certificate of Origin 1.1 です。
   DCO は法的な性質を持つ文書であるため、本文は翻訳せず、原文のまま
   掲載します。内容を確認する場合は、必ず英語原文を参照してください。

規則は単純で、以下を証明できる場合です::

        Developer's Certificate of Origin 1.1

        By making a contribution to this project, I certify that:

        (a) The contribution was created in whole or in part by me and I
            have the right to submit it under the open source license
            indicated in the file; or

        (b) The contribution is based upon previous work that, to the best
            of my knowledge, is covered under an appropriate open source
            license and I have the right under that license to submit that
            work with modifications, whether created in whole or in part
            by me, under the same open source license (unless I am
            permitted to submit under a different license), as indicated
            in the file; or

        (c) The contribution was provided directly to me by some other
            person who certified (a), (b) or (c) and I have not modified
            it.

        (d) I understand and agree that this project and the contribution
            are public and that a record of the contribution (including all
            personal information I submit with it, including my sign-off) is
            maintained indefinitely and may be redistributed consistent with
            this project or the open source license(s) involved.

上記を証明できる場合は、次のような行を追加します::

        Signed-off-by: Random J Developer <random@developer.example.org>

既知の身元を使用してください。匿名での貢献は認められません。
``git commit -s`` を使用すると、この行を自動的に追加できます。

revert にも ``Signed-off-by:`` を含める必要があります。
``git revert -s`` を使用すると、自動的に追加できます。

末尾に追加のタグを付ける人もいます。現時点では無視されますが、
社内手続きを示したり、sign-off に関する特記事項を記録したりするために
使用できます。

作者の SoB に続く追加の SoB（``Signed-off-by:``）は、パッチの開発には
関与せず、その取り扱いや転送を行った人によるものです。SoB の連鎖は、
パッチがメンテナーを経て最終的に Linus へ届いた実際の経路を反映する
必要があります。最初の SoB は、単独の主要作者であることを示します。


Acked-by:、Cc:、Co-developed-by: を使用する場合
------------------------------------------------

``Signed-off-by:`` タグは、署名者がパッチの開発に関与したか、
そのパッチの送付経路に関与したことを示します。

パッチの準備や取り扱いに直接関与していない人が、そのパッチへの
承認を表明し、記録に残したい場合は、パッチの変更履歴に
``Acked-by:`` 行を追加するよう依頼できます。

``Acked-by:`` は、何らかの形で影響を受けるコードに責任を持つ人、
またはそのコードに関与する人が使用することを意図しています。
最も一般的なのは、パッチの作成にも転送にも関与していない
メンテナーが使用する場合です。

``Acked-by:`` は、変更されるコードの元の作者など、その分野の
知識を持つ人、カーネル uAPI パッチをユーザー空間側からレビューする人、
または機能の主要な利用者など、その他の利害関係者も使用できます。
このような場合は、必要に応じて意味を明確にするために
``# Suffix`` を追加すると便利です::

    Acked-by: The Stakeholder <stakeholder@example.org> # As primary user

``Acked-by:`` は ``Signed-off-by:`` ほど正式なものではありません。
これは、承認した人が少なくともパッチをレビューし、受け入れる意思を
示したことの記録です。そのため、パッチをマージする人が、
「はい、問題なさそうです」という返答を手動で ``Acked-by:`` に
変換することがあります。ただし、通常は明示的な承認を求める方が
適切です。

``Acked-by:`` は ``Reviewed-by:`` よりも正式なものではありません。
たとえば、メンテナーはパッチが取り込まれることに同意していても、
``Reviewed-by:`` を付ける場合ほど十分にはレビューしていないことを
示すために使用できます。同様に、主要な利用者はパッチの技術的な
レビューを行っていなくても、全体的な方針、機能、または
ユーザー向けインターフェースに満足している場合があります。

``Acked-by:`` は、必ずしもパッチ全体への同意を示すものでは
ありません。たとえば、パッチが複数のサブシステムに影響し、
そのうち一つのサブシステムのメンテナーから ``Acked-by:`` が
付けられている場合、通常は、そのメンテナーが担当するコードに
影響する部分だけへの同意を示します。ここでは状況に応じた判断が
必要です。疑問がある場合は、メーリングリストアーカイブにある
元の議論を参照してください。この場合も、意味を明確にするために
``# Suffix`` を使用できます。

ある人がパッチにコメントする機会を得たものの、コメントしなかった
場合は、必要に応じてパッチに ``Cc:`` タグを追加できます。このタグは、
関心を持つ可能性のある人が議論に含まれていたことを記録します。
なお、これは、名前を記載される本人の明示的な許可なしに使用できる
可能性のある三つのタグのうちの一つです。詳細については、後述の
「人をタグ付けするには許可が必要」を参照してください。

``Co-developed-by:`` は、パッチが複数の開発者によって共同で作成された
ことを示します。複数の人が一つのパッチを共同で作成した場合に、
``From:`` タグで示される作者に加えて、共同作者の貢献を明記するために
使用します。

``Co-developed-by:`` は作者であることを示すため、各
``Co-developed-by:`` の直後には、対応する共同作者の
``Signed-off-by:`` を必ず記載しなければなりません。通常の sign-off
手続きが適用されます。つまり、作者が ``From:`` と
``Co-developed-by:`` のどちらで示されているかにかかわらず、
``Signed-off-by:`` タグの順序は、可能な限りパッチの時系列上の
経緯を反映する必要があります。特に、最後の ``Signed-off-by:`` は、
必ずパッチを提出する開発者のものでなければなりません。

なお、``From:`` タグで示される作者が、メールヘッダーの From 行に
記載された人物およびメールアドレスと同じ場合、``From:`` タグは
省略できます。

``From:`` の作者自身が提出するパッチの例::

    <changelog>

    Co-developed-by: First Co-Author <first@coauthor.example.org>
    Signed-off-by: First Co-Author <first@coauthor.example.org>
    Co-developed-by: Second Co-Author <second@coauthor.example.org>
    Signed-off-by: Second Co-Author <second@coauthor.example.org>
    Signed-off-by: From Author <from@author.example.org>

``Co-developed-by:`` に記載された作者が提出するパッチの例::

    From: From Author <from@author.example.org>

    <changelog>

    Co-developed-by: Random Co-Author <random@coauthor.example.org>
    Signed-off-by: Random Co-Author <random@coauthor.example.org>
    Signed-off-by: From Author <from@author.example.org>
    Co-developed-by: Submitting Co-Author <sub@coauthor.example.org>
    Signed-off-by: Submitting Co-Author <sub@coauthor.example.org>
