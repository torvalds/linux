.. SPDX-License-Identifier: GPL-2.0

.. _tw_submittingpatches:

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/submitting-patches.rst <submittingpatches>`

譯者::

        中文版維護者： 鍾宇 TripleX Chung <xxx.phy@gmail.com>
        中文版翻譯者： 鍾宇 TripleX Chung <xxx.phy@gmail.com>
                       時奎亮 Alex Shi <alex.shi@linux.alibaba.com>
        中文版校譯者： 李陽 Li Yang <leoyang.li@nxp.com>
                       王聰 Wang Cong <xiyou.wangcong@gmail.com>
                       胡皓文 Hu Haowen <src.res@email.cn>


如何讓你的改動進入內核
======================

對於想要將改動提交到 Linux 內核的個人或者公司來說，如果不熟悉「規矩」，
提交的流程會讓人畏懼。本文檔收集了一系列建議，這些建議可以大大的提高你
的改動被接受的機會.

以下文檔含有大量簡潔的建議， 具體請見：
:ref:`Documentation/process <development_process_main>`
同樣，:ref:`Documentation/translations/zh_TW/process/submit-checklist.rst <tw_submitchecklist>`
給出在提交代碼前需要檢查的項目的列表。如果你在提交一個驅動程序，那麼
同時閱讀一下:
:ref:`Documentation/process/submitting-drivers.rst <submittingdrivers>`

其中許多步驟描述了Git版本控制系統的默認行爲；如果您使用Git來準備補丁，
您將發現它爲您完成的大部分機械工作，儘管您仍然需要準備和記錄一組合理的
補丁。一般來說，使用git將使您作爲內核開發人員的生活更輕鬆。


0) 獲取當前源碼樹
-----------------

如果您沒有一個可以使用當前內核原始碼的存儲庫，請使用git獲取一個。您將要
從主線存儲庫開始，它可以通過以下方式獲取::

        git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

但是，請注意，您可能不希望直接針對主線樹進行開發。大多數子系統維護人員運
行自己的樹，並希望看到針對這些樹準備的補丁。請參見MAINTAINERS文件中子系
統的 **T:** 項以查找該樹，或者簡單地詢問維護者該樹是否未在其中列出。

仍然可以通過tarballs下載內核版本（如下一節所述），但這是進行內核開發的
一種困難的方式。

1) "diff -up"
-------------

使用 "diff -up" 或者 "diff -uprN" 來創建補丁。

所有內核的改動，都是以補丁的形式呈現的，補丁由 diff(1) 生成。創建補丁的
時候，要確認它是以 "unified diff" 格式創建的，這種格式由 diff(1) 的 '-u'
參數生成。而且，請使用 '-p' 參數，那樣會顯示每個改動所在的C函數，使得
產生的補丁容易讀得多。補丁應該基於內核原始碼樹的根目錄，而不是裡邊的任
何子目錄。

爲一個單獨的文件創建補丁，一般來說這樣做就夠了::

        SRCTREE=linux
        MYFILE=drivers/net/mydriver.c

        cd $SRCTREE
        cp $MYFILE $MYFILE.orig
        vi $MYFILE      # make your change
        cd ..
        diff -up $SRCTREE/$MYFILE{.orig,} > /tmp/patch

爲多個文件創建補丁，你可以解開一個沒有修改過的內核原始碼樹，然後和你自
己的代碼樹之間做 diff 。例如::

        MYSRC=/devel/linux

        tar xvfz linux-3.19.tar.gz
        mv linux-3.19 linux-3.19-vanilla
        diff -uprN -X linux-3.19-vanilla/Documentation/dontdiff \
                linux-3.19-vanilla $MYSRC > /tmp/patch

"dontdiff" 是內核在編譯的時候產生的文件的列表，列表中的文件在 diff(1)
產生的補丁里會被跳過。

確定你的補丁里沒有包含任何不屬於這次補丁提交的額外文件。記得在用diff(1)
生成補丁之後，審閱一次補丁，以確保準確。

如果你的改動很散亂，你應該研究一下如何將補丁分割成獨立的部分，將改動分
割成一系列合乎邏輯的步驟。這樣更容易讓其他內核開發者審核，如果你想你的
補丁被接受，這是很重要的。請參閱：
:ref:`tw_split_changes`

如果你用 ``git`` , ``git rebase -i`` 可以幫助你這一點。如果你不用 ``git``,
``quilt`` <https://savannah.nongnu.org/projects/quilt> 另外一個流行的選擇。

.. _tw_describe_changes:

2) 描述你的改動
---------------

描述你的問題。無論您的補丁是一行錯誤修復還是5000行新功能，都必須有一個潛在
的問題激勵您完成這項工作。讓審稿人相信有一個問題值得解決，讓他們讀完第一段
是有意義的。

描述用戶可見的影響。直接崩潰和鎖定是相當有說服力的，但並不是所有的錯誤都那麼
明目張胆。即使在代碼審查期間發現了這個問題，也要描述一下您認爲它可能對用戶產
生的影響。請記住，大多數Linux安裝運行的內核來自二級穩定樹或特定於供應商/產品
的樹，只從上游精選特定的補丁，因此請包含任何可以幫助您將更改定位到下游的內容：
觸發的場景、DMESG的摘錄、崩潰描述、性能回歸、延遲尖峯、鎖定等。

量化優化和權衡。如果您聲稱在性能、內存消耗、堆棧占用空間或二進位大小方面有所
改進，請包括支持它們的數字。但也要描述不明顯的成本。優化通常不是免費的，而是
在CPU、內存和可讀性之間進行權衡；或者，探索性的工作，在不同的工作負載之間進
行權衡。請描述優化的預期缺點，以便審閱者可以權衡成本和收益。

一旦問題建立起來，就要詳細地描述一下您實際在做什麼。對於審閱者來說，用簡單的
英語描述代碼的變化是很重要的，以驗證代碼的行爲是否符合您的意願。

如果您將補丁描述寫在一個表單中，這個表單可以很容易地作爲「提交日誌」放入Linux
的原始碼管理系統git中，那麼維護人員將非常感謝您。見 :ref:`tw_explicit_in_reply_to`.

每個補丁只解決一個問題。如果你的描述開始變長，這就表明你可能需要拆分你的補丁。
請見 :ref:`tw_split_changes`

提交或重新提交修補程序或修補程序系列時，請包括完整的修補程序說明和理由。不要
只說這是補丁（系列）的第幾版。不要期望子系統維護人員引用更早的補丁版本或引用
URL來查找補丁描述並將其放入補丁中。也就是說，補丁（系列）及其描述應該是獨立的。
這對維護人員和審查人員都有好處。一些評審者可能甚至沒有收到補丁的早期版本。

描述你在命令語氣中的變化，例如「make xyzzy do frotz」而不是「[這個補丁]make
xyzzy do frotz」或「[我]changed xyzzy to do frotz」，就好像你在命令代碼庫改變
它的行爲一樣。

如果修補程序修復了一個記錄的bug條目，請按編號和URL引用該bug條目。如果補丁來
自郵件列表討論，請給出郵件列表存檔的URL；使用帶有 ``Message-ID`` 的
https://lore.kernel.org/ 重定向，以確保連結不會過時。

但是，在沒有外部資源的情況下，儘量讓你的解釋可理解。除了提供郵件列表存檔或
bug的URL之外，還要總結需要提交補丁的相關討論要點。

如果您想要引用一個特定的提交，不要只引用提交的 SHA-1 ID。還請包括提交的一行
摘要，以便於審閱者了解它是關於什麼的。例如::

        Commit e21d2170f36602ae2708 ("video: remove unnecessary
        platform_set_drvdata()") removed the unnecessary
        platform_set_drvdata(), but left the variable "dev" unused,
        delete it.

您還應該確保至少使用前12位 SHA-1 ID. 內核存儲庫包含*許多*對象，使與較短的ID
發生衝突的可能性很大。記住，即使現在不會與您的六個字符ID發生衝突，這種情況
可能五年後改變。

如果修補程序修復了特定提交中的錯誤，例如，使用 ``git bisct`` ，請使用帶有前
12個字符SHA-1 ID 的"Fixes:"標記和單行摘要。爲了簡化不要將標記拆分爲多個，
行、標記不受分析腳本「75列換行」規則的限制。例如::

        Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

下列 ``git config`` 設置可以添加讓 ``git log``, ``git show`` 漂亮的顯示格式::

	[core]
		abbrev = 12
	[pretty]
		fixes = Fixes: %h (\"%s\")

.. _tw_split_changes:

3) 拆分你的改動
---------------

將每個邏輯更改分隔成一個單獨的補丁。

例如，如果你的改動里同時有bug修正和性能優化，那麼把這些改動拆分到兩個或
者更多的補丁文件中。如果你的改動包含對API的修改，並且修改了驅動程序來適
應這些新的API，那麼把這些修改分成兩個補丁。

另一方面，如果你將一個單獨的改動做成多個補丁文件，那麼將它們合併成一個
單獨的補丁文件。這樣一個邏輯上單獨的改動只被包含在一個補丁文件里。

如果有一個補丁依賴另外一個補丁來完成它的改動，那沒問題。簡單的在你的補
丁描述里指出「這個補丁依賴某補丁」就好了。

在將您的更改劃分爲一系列補丁時，要特別注意確保內核在系列中的每個補丁之後
都能正常構建和運行。使用 ``git bisect`` 來追蹤問題的開發者可能會在任何時
候分割你的補丁系列；如果你在中間引入錯誤，他們不會感謝你。

如果你不能將補丁濃縮成更少的文件，那麼每次大約發送出15個，然後等待審查
和集成。

4) 檢查你的更改風格
-------------------

檢查您的補丁是否存在基本樣式衝突，詳細信息可在
:ref:`Documentation/translations/zh_TW/process/coding-style.rst <tw_codingstyle>`
中找到。如果不這樣做，只會浪費審稿人的時間，並且會導致你的補丁被拒絕，甚至
可能沒有被閱讀。

一個重要的例外是在將代碼從一個文件移動到另一個文件時——在這種情況下，您不應
該在移動代碼的同一個補丁中修改移動的代碼。這清楚地描述了移動代碼和您的更改
的行爲。這大大有助於審查實際差異，並允許工具更好地跟蹤代碼本身的歷史。

在提交之前，使用補丁樣式檢查程序檢查補丁（scripts/check patch.pl）。不過，
請注意，樣式檢查程序應該被視爲一個指南，而不是作爲人類判斷的替代品。如果您
的代碼看起來更好，但有違規行爲，那麼最好不要使用它。

檢查者報告三個級別：

 - ERROR：很可能出錯的事情
 - WARNING：需要仔細審查的事項
 - CHECK：需要思考的事情

您應該能夠判斷您的補丁中存在的所有違規行爲。

5) 選擇補丁收件人
-----------------

您應該總是在任何補丁上複製相應的子系統維護人員，以獲得他們維護的代碼；查看
維護人員文件和原始碼修訂歷史記錄，以了解這些維護人員是誰。腳本
scripts/get_Maintainer.pl在這個步驟中非常有用。如果您找不到正在工作的子系統
的維護人員，那麼Andrew Morton（akpm@linux-foundation.org）將充當最後的維護
人員。

您通常還應該選擇至少一個郵件列表來接收補丁集的。linux-kernel@vger.kernel.org
作爲最後一個解決辦法的列表，但是這個列表上的體積已經引起了許多開發人員的拒絕。
在MAINTAINERS文件中查找子系統特定的列表；您的補丁可能會在那裡得到更多的關注。
不過，請不要發送垃圾郵件到無關的列表。

許多與內核相關的列表託管在vger.kernel.org上；您可以在
http://vger.kernel.org/vger-lists.html 上找到它們的列表。不過，也有與內核相關
的列表託管在其他地方。

不要一次發送超過15個補丁到vger郵件列表！！！！

Linus Torvalds 是決定改動能否進入 Linux 內核的最終裁決者。他的 e-mail
地址是 <torvalds@linux-foundation.org> 。他收到的 e-mail 很多，所以一般
的說，最好別給他發 e-mail。

如果您有修復可利用安全漏洞的補丁，請將該補丁發送到 security@kernel.org。對於
嚴重的bug，可以考慮短期暫停以允許分銷商向用戶發布補丁；在這種情況下，顯然不應
將補丁發送到任何公共列表。

修復已發布內核中嚴重錯誤的補丁程序應該指向穩定版維護人員，方法是放這樣的一行::

        Cc: stable@vger.kernel.org

進入補丁的簽准區（注意，不是電子郵件收件人）。除了這個文件之外，您還應該閱讀
:ref:`Documentation/process/stable-kernel-rules.rst <stable_kernel_rules>`

但是，請注意，一些子系統維護人員希望得出他們自己的結論，即哪些補丁應該被放到
穩定的樹上。尤其是網絡維護人員，不希望看到單個開發人員在補丁中添加像上面這樣
的行。

如果更改影響到用戶和內核接口，請向手冊頁維護人員（如維護人員文件中所列）發送
手冊頁補丁，或至少發送更改通知，以便一些信息進入手冊頁。還應將用戶空間API
更改複製到 linux-api@vger.kernel.org。

6) 沒有 MIME 編碼，沒有連結，沒有壓縮，沒有附件，只有純文本
-----------------------------------------------------------

Linus 和其他的內核開發者需要閱讀和評論你提交的改動。對於內核開發者來說
，可以「引用」你的改動很重要，使用一般的 e-mail 工具，他們就可以在你的
代碼的任何位置添加評論。

因爲這個原因，所有的提交的補丁都是 e-mail 中「內嵌」的。

.. warning::
   如果你使用剪切-粘貼你的補丁，小心你的編輯器的自動換行功能破壞你的補丁

不要將補丁作爲 MIME 編碼的附件，不管是否壓縮。很多流行的 e-mail 軟體不
是任何時候都將 MIME 編碼的附件當作純文本發送的，這會使得別人無法在你的
代碼中加評論。另外，MIME 編碼的附件會讓 Linus 多花一點時間來處理，這就
降低了你的改動被接受的可能性。

例外：如果你的郵遞員弄壞了補丁，那麼有人可能會要求你使用mime重新發送補丁

請參閱 :ref:`Documentation/translations/zh_TW/process/email-clients.rst <tw_email_clients>`
以獲取有關配置電子郵件客戶端以使其不受影響地發送修補程序的提示。

7) e-mail 的大小
----------------

大的改動對郵件列表不合適，對某些維護者也不合適。如果你的補丁，在不壓縮
的情況下，超過了300kB，那麼你最好將補丁放在一個能通過 internet 訪問的服
務器上，然後用指向你的補丁的 URL 替代。但是請注意，如果您的補丁超過了
300kb，那麼它幾乎肯定需要被破壞。

8）回複評審意見
---------------

你的補丁幾乎肯定會得到評審者對補丁改進方法的評論。您必須對這些評論作出
回應；讓補丁被忽略的一個好辦法就是忽略審閱者的意見。不會導致代碼更改的
意見或問題幾乎肯定會帶來注釋或變更日誌的改變，以便下一個評審者更好地了解
正在發生的事情。

一定要告訴審稿人你在做什麼改變，並感謝他們的時間。代碼審查是一個累人且
耗時的過程，審查人員有時會變得暴躁。即使在這種情況下，也要禮貌地回應並
解決他們指出的問題。

9）不要洩氣或不耐煩
-------------------

提交更改後，請耐心等待。審閱者是忙碌的人，可能無法立即訪問您的修補程序。

曾幾何時，補丁曾在沒有評論的情況下消失在空白中，但開發過程比現在更加順利。
您應該在一周左右的時間內收到評論；如果沒有收到評論，請確保您已將補丁發送
到正確的位置。在重新提交或聯繫審閱者之前至少等待一周-在諸如合併窗口之類的
繁忙時間可能更長。

10）主題中包含 PATCH
--------------------

由於到linus和linux內核的電子郵件流量很高，通常會在主題行前面加上[PATCH]
前綴. 這使Linus和其他內核開發人員更容易將補丁與其他電子郵件討論區分開。

11）簽署你的作品-開發者原始認證
-------------------------------

爲了加強對誰做了何事的追蹤，尤其是對那些透過好幾層的維護者的補丁，我們
建議在發送出去的補丁上加一個 「sign-off」 的過程。

"sign-off" 是在補丁的注釋的最後的簡單的一行文字，認證你編寫了它或者其他
人有權力將它作爲開放原始碼的補丁傳遞。規則很簡單：如果你能認證如下信息:

開發者來源證書 1.1
^^^^^^^^^^^^^^^^^^

對於本項目的貢獻，我認證如下信息：

      （a）這些貢獻是完全或者部分的由我創建，我有權利以文件中指出
           的開放原始碼許可證提交它；或者
      （b）這些貢獻基於以前的工作，據我所知，這些以前的工作受恰當的開放
           原始碼許可證保護，而且，根據許可證，我有權提交修改後的貢獻，
           無論是完全還是部分由我創造，這些貢獻都使用同一個開放原始碼許可證
           （除非我被允許用其它的許可證），正如文件中指出的；或者
      （c）這些貢獻由認證（a），（b）或者（c）的人直接提供給我，而
           且我沒有修改它。
      （d）我理解並同意這個項目和貢獻是公開的，貢獻的記錄（包括我
           一起提交的個人記錄，包括 sign-off ）被永久維護並且可以和這個項目
           或者開放原始碼的許可證同步地再發行。

那麼加入這樣一行::

       Signed-off-by: Random J Developer <random@developer.example.org>

使用你的真名（抱歉，不能使用假名或者匿名。）

有人在最後加上標籤。現在這些東西會被忽略，但是你可以這樣做，來標記公司
內部的過程，或者只是指出關於 sign-off 的一些特殊細節。

如果您是子系統或分支維護人員，有時需要稍微修改收到的補丁，以便合併它們，
因爲樹和提交者中的代碼不完全相同。如果你嚴格遵守規則（c），你應該要求提交者
重新發布，但這完全是在浪費時間和精力。規則（b）允許您調整代碼，但是更改一個
提交者的代碼並讓他認可您的錯誤是非常不禮貌的。要解決此問題，建議在最後一個
由簽名行和您的行之間添加一行，指示更改的性質。雖然這並不是強制性的，但似乎
在描述前加上您的郵件和/或姓名（全部用方括號括起來），這足以讓人注意到您對最
後一分鐘的更改負有責任。例如::

	Signed-off-by: Random J Developer <random@developer.example.org>
	[lucky@maintainer.example.org: struct foo moved from foo.c to foo.h]
	Signed-off-by: Lucky K Maintainer <lucky@maintainer.example.org>

如果您維護一個穩定的分支機構，同時希望對作者進行致謝、跟蹤更改、合併修復並
保護提交者不受投訴，那麼這種做法尤其有用。請注意，在任何情況下都不能更改作者
的ID（From 頭），因爲它是出現在更改日誌中的標識。

對回合（back-porters）的特別說明：在提交消息的頂部（主題行之後）插入一個補丁
的起源指示似乎是一種常見且有用的實踐，以便於跟蹤。例如，下面是我們在3.x穩定
版本中看到的內容::

  Date:   Tue Oct 7 07:26:38 2014 -0400

    libata: Un-break ATA blacklist

    commit 1c40279960bcd7d52dbdf1d466b20d24b99176c8 upstream.

還有， 這裡是一個舊版內核中的一個回合補丁::

    Date:   Tue May 13 22:12:27 2008 +0200

        wireless, airo: waitbusy() won't delay

        [backport of 2.6 commit b7acbdfbd1f277c1eb23f344f899cfa4cd0bf36a]

12）何時使用Acked-by:，CC:，和Co-Developed by:
----------------------------------------------

Singed-off-by: 標記表示簽名者參與了補丁的開發，或者他/她在補丁的傳遞路徑中。

如果一個人沒有直接參與補丁的準備或處理，但希望表示並記錄他們對補丁的批准，
那麼他們可以要求在補丁的變更日誌中添加一個 Acked-by:

Acked-by：通常由受影響代碼的維護者使用，當該維護者既沒有貢獻也沒有轉發補丁時。

Acked-by: 不像簽字人那樣正式。這是一個記錄，確認人至少審查了補丁，並表示接受。
因此，補丁合併有時會手動將Acker的「Yep，looks good to me」轉換爲 Acked-By：（但
請注意，通常最好要求一個明確的Ack）。

Acked-by：不一定表示對整個補丁的確認。例如，如果一個補丁影響多個子系統，並且
有一個：來自一個子系統維護者，那麼這通常表示只確認影響維護者代碼的部分。這裡
應該仔細判斷。如有疑問，應參考郵件列表檔案中的原始討論。

如果某人有機會對補丁進行評論，但沒有提供此類評論，您可以選擇在補丁中添加 ``Cc:``
這是唯一一個標籤，它可以在沒有被它命名的人顯式操作的情況下添加，但它應該表明
這個人是在補丁上抄送的。討論中包含了潛在利益相關方。

Co-developed-by: 聲明補丁是由多個開發人員共同創建的；當幾個人在一個補丁上工
作時，它用於將屬性賦予共同作者（除了 From: 所賦予的作者之外）。因爲
Co-developed-by: 表示作者身份，所以每個共同開發人：必須緊跟在相關合作作者的
簽名之後。標準的簽核程序要求：標記的簽核順序應儘可能反映補丁的時間歷史，而不
管作者是通過 From ：還是由 Co-developed-by: 共同開發的。值得注意的是，最後一
個簽字人：必須始終是提交補丁的開發人員。

注意，當作者也是電子郵件標題「發件人：」行中列出的人時，「From: 」 標記是可選的。

作者提交的補丁程序示例::

	<changelog>

	Co-developed-by: First Co-Author <first@coauthor.example.org>
	Signed-off-by: First Co-Author <first@coauthor.example.org>
	Co-developed-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: From Author <from@author.example.org>

合作開發者提交的補丁示例::

	From: From Author <from@author.example.org>

	<changelog>

	Co-developed-by: Random Co-Author <random@coauthor.example.org>
	Signed-off-by: Random Co-Author <random@coauthor.example.org>
	Signed-off-by: From Author <from@author.example.org>
	Co-developed-by: Submitting Co-Author <sub@coauthor.example.org>
	Signed-off-by: Submitting Co-Author <sub@coauthor.example.org>


13）使用報告人：、測試人：、審核人：、建議人：、修復人：
--------------------------------------------------------

Reported-by: 給那些發現錯誤並報告錯誤的人致謝，它希望激勵他們在將來再次幫助
我們。請注意，如果bug是以私有方式報告的，那麼在使用Reported-by標記之前，請
先請求權限。

Tested-by: 標記表示補丁已由指定的人（在某些環境中）成功測試。這個標籤通知
維護人員已經執行了一些測試，爲將來的補丁提供了一種定位測試人員的方法，並確
保測試人員的信譽。

Reviewed-by：相反，根據審查人的聲明，表明該補丁已被審查並被認爲是可接受的：


審查人的監督聲明
^^^^^^^^^^^^^^^^

通過提供我的 Reviewed-by，我聲明：

        (a) 我已經對這個補丁進行了一次技術審查，以評估它是否適合被包含到
            主線內核中。

        (b) 與補丁相關的任何問題、顧慮或問題都已反饋給提交者。我對提交者對
            我的評論的回應感到滿意。

        (c) 雖然這一提交可能會改進一些東西，但我相信，此時，（1）對內核
            進行了有價值的修改，（2）沒有包含爭論中涉及的已知問題。

        (d) 雖然我已經審查了補丁並認爲它是健全的，但我不會（除非另有明確
            說明）作出任何保證或保證它將在任何給定情況下實現其規定的目的
            或正常運行。

Reviewed-by 是一種觀點聲明，即補丁是對內核的適當修改，沒有任何遺留的嚴重技術
問題。任何感興趣的審閱者（完成工作的人）都可以爲一個補丁提供一個 Review-by
標籤。此標籤用於向審閱者提供致謝，並通知維護者已在修補程序上完成的審閱程度。
Reviewed-by: 當由已知了解主題區域並執行徹底檢查的審閱者提供時，通常會增加
補丁進入內核的可能性。

Suggested-by: 表示補丁的想法是由指定的人提出的，並確保將此想法歸功於指定的
人。請注意，未經許可，不得添加此標籤，特別是如果該想法未在公共論壇上發布。
這就是說，如果我們勤快地致謝我們的創意者，他們很有希望在未來得到鼓舞，再次
幫助我們。

Fixes: 指示補丁在以前的提交中修復了一個問題。它可以很容易地確定錯誤的來源，
這有助於檢查錯誤修復。這個標記還幫助穩定內核團隊確定應該接收修復的穩定內核
版本。這是指示補丁修復的錯誤的首選方法。請參閱 :ref:`tw_describe_changes`
描述您的更改以了解更多詳細信息。

.. _tw_the_canonical_patch_format:

12）標準補丁格式
----------------

本節描述如何格式化補丁本身。請注意，如果您的補丁存儲在 ``Git`` 存儲庫中，則
可以使用 ``git format-patch`` 進行正確的補丁格式設置。但是，這些工具無法創建
必要的文本，因此請務必閱讀下面的說明。

標準的補丁，標題行是::

    Subject: [PATCH 001/123] 子系統:一句話概述

標準補丁的信體存在如下部分：

  - 一個 "from" 行指出補丁作者。後跟空行（僅當發送修補程序的人不是作者時才需要）。

  - 解釋的正文，行以75列包裝，這將被複製到永久變更日誌來描述這個補丁。

  - 一個空行

  - 上面描述的「Signed-off-by」 行，也將出現在更改日誌中。

  - 只包含 ``---`` 的標記線。

  - 任何其他不適合放在變更日誌的注釋。

  - 實際補丁（ ``diff`` 輸出）。

標題行的格式，使得對標題行按字母序排序非常的容易 - 很多 e-mail 客戶端都
可以支持 - 因爲序列號是用零填充的，所以按數字排序和按字母排序是一樣的。

e-mail 標題中的「子系統」標識哪個內核子系統將被打補丁。

e-mail 標題中的「一句話概述」扼要的描述 e-mail 中的補丁。「一句話概述」
不應該是一個文件名。對於一個補丁系列（「補丁系列」指一系列的多個相關補
丁），不要對每個補丁都使用同樣的「一句話概述」。

記住 e-mail 的「一句話概述」會成爲該補丁的全局唯一標識。它會蔓延到 git
的改動記錄里。然後「一句話概述」會被用在開發者的討論里，用來指代這個補
丁。用戶將希望通過 google 來搜索"一句話概述"來找到那些討論這個補丁的文
章。當人們在兩三個月後使用諸如 ``gitk`` 或 ``git log --oneline`` 之類
的工具查看數千個補丁時，也會很快看到它。

出於這些原因，概述必須不超過70-75個字符，並且必須描述補丁的更改以及爲
什麼需要補丁。既要簡潔又要描述性很有挑戰性，但寫得好的概述應該這樣做。

概述的前綴可以用方括號括起來：「Subject: [PATCH <tag>...] <概述>」。標記
不被視爲概述的一部分，而是描述應該如何處理補丁。如果補丁的多個版本已發
送出來以響應評審（即「v1，v2，v3」）或「rfc」，以指示評審請求，那麼通用標記
可能包括版本描述符。如果一個補丁系列中有四個補丁，那麼各個補丁可以這樣
編號：1/4、2/4、3/4、4/4。這可以確保開發人員了解補丁應用的順序，並且他們
已經查看或應用了補丁系列中的所有補丁。

一些標題的例子::

    Subject: [patch 2/5] ext2: improve scalability of bitmap searching
    Subject: [PATCHv2 001/207] x86: fix eflags tracking

"From" 行是信體裡的最上面一行，具有如下格式：
        From: Patch Author <author@example.com>

"From" 行指明在永久改動日誌里，誰會被確認爲作者。如果沒有 "From" 行，那
麼郵件頭裡的 "From: " 行會被用來決定改動日誌中的作者。

說明的主題將會被提交到永久的原始碼改動日誌里，因此對那些早已經不記得和
這個補丁相關的討論細節的有能力的讀者來說，是有意義的。包括補丁程序定位
錯誤的（內核日誌消息、OOPS消息等）症狀，對於搜索提交日誌以尋找適用補丁的人
尤其有用。如果一個補丁修復了一個編譯失敗，那麼可能不需要包含所有編譯失敗；
只要足夠讓搜索補丁的人能夠找到它就行了。與概述一樣，既要簡潔又要描述性。

"---" 標記行對於補丁處理工具要找到哪裡是改動日誌信息的結束，是不可缺少
的。

對於 "---" 標記之後的額外註解，一個好的用途就是用來寫 diffstat，用來顯
示修改了什麼文件和每個文件都增加和刪除了多少行。diffstat 對於比較大的補
丁特別有用。其餘那些只是和時刻或者開發者相關的註解，不合適放到永久的改
動日誌里的，也應該放這裡。
使用 diffstat的選項 "-p 1 -w 70" 這樣文件名就會從內核原始碼樹的目錄開始
，不會占用太寬的空間（很容易適合80列的寬度，也許會有一些縮進。）

在後面的參考資料中能看到適當的補丁格式的更多細節。

.. _tw_explicit_in_reply_to:

15) 明確回覆郵件頭(In-Reply-To)
-------------------------------

手動添加回復補丁的的標題頭(In-Reply_To:) 是有幫助的（例如，使用 ``git send-email`` ）
將補丁與以前的相關討論關聯起來，例如，將bug修復程序連結到電子郵件和bug報告。
但是，對於多補丁系列，最好避免在回復時使用連結到該系列的舊版本。這樣，
補丁的多個版本就不會成爲電子郵件客戶端中無法管理的引用序列。如果連結有用，
可以使用 https://lore.kernel.org/ 重定向器（例如，在封面電子郵件文本中）
連結到補丁系列的早期版本。

16) 發送git pull請求
--------------------

如果您有一系列補丁，那麼讓維護人員通過git pull操作將它們直接拉入子系統存儲
庫可能是最方便的。但是，請注意，從開發人員那裡獲取補丁比從郵件列表中獲取補
丁需要更高的信任度。因此，許多子系統維護人員不願意接受請求，特別是來自新的
未知開發人員的請求。如果有疑問，您可以在封面郵件中使用pull 請求作爲補丁系列
正常發布的一個選項，讓維護人員可以選擇使用其中之一。

pull 請求的主題行中應該有[Git Pull]。請求本身應該在一行中包含存儲庫名稱和
感興趣的分支；它應該看起來像::

  Please pull from

      git://jdelvare.pck.nerim.net/jdelvare-2.6 i2c-for-linus

  to get these changes:


pull 請求還應該包含一條整體消息，說明請求中將包含什麼，一個補丁本身的 ``Git shortlog``
以及一個顯示補丁系列整體效果的 ``diffstat`` 。當然，將所有這些信息收集在一起
的最簡單方法是讓 ``git`` 使用 ``git request-pull`` 命令爲您完成這些工作。

一些維護人員（包括Linus）希望看到來自已簽名提交的請求；這增加了他們對你的
請求信心。特別是，在沒有簽名標籤的情況下，Linus 不會從像 Github 這樣的公共
託管站點拉請求。

創建此類簽名的第一步是生成一個 GNRPG 密鑰，並由一個或多個核心內核開發人員對
其進行簽名。這一步對新開發人員來說可能很困難，但沒有辦法繞過它。參加會議是
找到可以簽署您的密鑰的開發人員的好方法。

一旦您在Git 中準備了一個您希望有人拉的補丁系列，就用 ``git tag -s`` 創建一
個簽名標記。這將創建一個新標記，標識該系列中的最後一次提交，並包含用您的私
鑰創建的簽名。您還可以將changelog樣式的消息添加到標記中；這是一個描述拉請求
整體效果的理想位置。

如果維護人員將要從中提取的樹不是您正在使用的存儲庫，請不要忘記將已簽名的標記
顯式推送到公共樹。

生成拉請求時，請使用已簽名的標記作爲目標。這樣的命令可以實現::

  git request-pull master git://my.public.tree/linux.git my-signed-tag

參考文獻
--------

Andrew Morton, "The perfect patch" (tpp).
  <https://www.ozlabs.org/~akpm/stuff/tpp.txt>

Jeff Garzik, "Linux kernel patch submission format".
  <https://web.archive.org/web/20180829112450/http://linux.yyz.us/patch-format.html>

Greg Kroah-Hartman, "How to piss off a kernel subsystem maintainer".
  <http://www.kroah.com/log/linux/maintainer.html>

  <http://www.kroah.com/log/linux/maintainer-02.html>

  <http://www.kroah.com/log/linux/maintainer-03.html>

  <http://www.kroah.com/log/linux/maintainer-04.html>

  <http://www.kroah.com/log/linux/maintainer-05.html>

  <http://www.kroah.com/log/linux/maintainer-06.html>

NO!!!! No more huge patch bombs to linux-kernel@vger.kernel.org people!
  <https://lore.kernel.org/r/20050711.125305.08322243.davem@davemloft.net>

Kernel Documentation/process/coding-style.rst:
  :ref:`Documentation/translations/zh_TW/process/coding-style.rst <tw_codingstyle>`

Linus Torvalds's mail on the canonical patch format:
  <https://lore.kernel.org/r/Pine.LNX.4.58.0504071023190.28951@ppc970.osdl.org>

Andi Kleen, "On submitting kernel patches"
  Some strategies to get difficult or controversial changes in.

  http://halobates.de/on-submitting-patches.pdf

