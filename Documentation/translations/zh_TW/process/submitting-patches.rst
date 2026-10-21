.. SPDX-License-Identifier: GPL-2.0-or-later

.. include:: ../disclaimer-zh_TW.rst

.. _tw_submittingpatches:

:Original: Documentation/process/submitting-patches.rst

:譯者:
 - 鍾宇 TripleX Chung <xxx.phy@gmail.com>
 - 時奎亮 Alex Shi <alexs@kernel.org>
 - 吳想成 Wu XiangCheng <bobwxc@email.cn>

:校譯:
 - 李陽 Li Yang <leoyang.li@nxp.com>
 - 王聰 Wang Cong <xiyou.wangcong@gmail.com>
 - 胡皓文 Hu Haowen <2023002089@link.tyut.edu.cn>
 - 葉宸佑 Chen-Yu Yeh <chenyou910331@gmail.com>


提交補丁：如何讓你的改動進入核心
================================

對於想要將改動提交到 Linux 核心的個人或者公司來說，如果不熟悉“規矩”，
提交的流程會讓人畏懼。本文件包含了一系列建議，可以大大提高你
的改動被接受的機會.

本文件以較為簡潔的行文給出了大量建議。關於核心開發流程如何進行的詳細資訊，
參見： Documentation/translations/zh_TW/process/development-process.rst 。
Documentation/translations/zh_TW/process/submit-checklist.rst 給出了一系列
提交補丁之前要檢查的事項。設備樹相關的補丁，請參閱
Documentation/devicetree/bindings/submitting-patches.rst 。

本文件假設您正在使用 ``git`` 準備你的補丁。如果您不熟悉 ``git`` ，最好學習
如何使用它，這將使您作為核心開發人員的生活變得更加輕鬆。

部分子系統和維護人員的樹有一些關於其工作流程和要求的額外資訊，請參閱
Documentation/process/maintainer-handbooks.rst 。

獲取當前源碼樹
--------------

如果您手頭沒有當前核心原始程式碼的儲存庫，請使用 ``git`` 獲取一份。您需要先獲取
主線儲存庫，它可以透過以下命令拉取::

    git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

但是，請注意，您可能不想直接針對主線樹進行開發。大多數子系統維護人員運
行自己的樹，並希望看到針對這些樹準備的補丁。請參見MAINTAINERS檔案中子系
統的 **T:** 項以查找該樹，或者直接詢問維護者該樹是否未在其中列出。

.. _tw_describe_changes:

描述你的改動
------------

描述你的問題。無論您的補丁是一行錯誤修復還是5000行新功能，都必須有一個潛在
的問題激勵您完成這項工作。說服審閱者相信有一個問題值得解決，讓他們讀完第一段
後就能明白這一點。

描述使用者可見的影響。直接崩潰和鎖定是相當有說服力的，但並不是所有的錯誤都
那麼明目張膽。即使在程式碼審閱期間發現了這個問題，也要描述一下您認為它可能
對使用者產生的影響。請記住，大多數Linux安裝執行的核心來自二級穩定樹或特定
於供應商/產品的樹，只從上游精選特定的補丁，因此請包含任何可以幫助您將更改
定位到下游的內容：觸發的場景、DMESG的摘錄、崩潰描述、效能迴歸、延遲尖峯、
鎖定等。

品質最佳化和權衡。如果您聲稱在效能、記憶體消耗、堆疊佔用空間或二進位大小方
面有所改進，請包括支援它們的資料。但也要描述不明顯的成本。最佳化通常不是零
成本的，而是在CPU、記憶體和可讀性之間進行權衡；或者，做探索性的工作，在不
同的工作負載之間進行權衡。請描述最佳化的預期缺點，以便審閱者可以權衡成本和
收益。

提出問題之後，就要詳細地描述一下您實際在做的技術細節。對於審閱者來說，用簡練的
英語描述程式碼的變化是很重要的，以驗證程式碼的行為是否符合您的意圖。

如果您將補丁描述寫成“標準格式”，可以很容易地作為“提交日誌”放入Linux的源代
碼管理系統 ``git`` 中，那麼維護人員將非常感謝您。
參見 :ref:`tw_the_canonical_patch_format` 。

每個補丁只解決一個問題。如果你的描述開始變長，這就表明你可能需要拆分你的補丁。
請見 :ref:`tw_split_changes` 。

提交或重新提交補丁或補丁系列時，請包括完整的補丁說明和理由。不要只說這是補丁
（系列）的第幾版。不要期望子系統維護人員引用更早的補丁版本或引用URL來查
找補丁描述並將其放入補丁中。也就是說，補丁（系列）及其描述應該是獨立的。這
對維護人員和審閱者都有好處。一些審閱者可能甚至沒有收到補丁的早期版本。

用祈使句描述你的變更，例如“make xyzzy do frotz”而不是“[This patch]make
xyzzy do frotz”或“[I]changed xyzzy to do frotz”，就好像你在命令程式碼庫改變
它的行為一樣。

如果您想要引用一個特定的提交，不要只引用提交的SHA-1 ID。還請包括提交的一行
摘要，以便於審閱者瞭解它是關於什麼的。例如::

        Commit e21d2170f36602ae2708 ("video: remove unnecessary
        platform_set_drvdata()") removed the unnecessary
        platform_set_drvdata(), but left the variable "dev" unused,
        delete it.

您還應該確保至少使用前12位SHA-1 ID。核心儲存庫包含 *許多* 物件，使較短的ID
發生衝突的可能性很大。記住，即使現在不會與您的六個字元ID發生衝突，這種情況
也可能在五年後改變。

如果該變更的相關討論或背景資訊可以在網上查閱，請加上“Link:”標籤指向它。例如
你的補丁修復了一個缺陷，需要添加一個帶有URL的標籤指向郵件列表存檔或缺陷追蹤器
的相關報告；如果該補丁是由一些早先郵件列表討論或網路上的記錄引起的，請指向它。

當連結到郵件列表存檔時，請首選lore.kernel.org郵件存檔服務。用郵件中的
``Message-ID`` 頭（去掉尖括號）可以建立連結URL。例如::

    Link: https://lore.kernel.org/30th.anniversary.repost@klaava.Helsinki.FI

請檢查該連結以確保可用且指向正確的郵件。

不過，在沒有外部資源的情況下，也要儘量讓你的解釋可理解。除了提供郵件列表存檔或
缺陷的URL之外，還要需要總結該補丁的相關討論要點。

如果補丁修復了特定提交中的錯誤，例如使用 ``git bisect`` 發現了一個問題，請使用
帶有至少前12個字元SHA-1 ID的“Fixes:”標籤和單行摘要。為了簡化解析腳本，不要將該
標籤拆分為多行，標籤不受“75列換行”規則的限制。例如::

  Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

下列 ``git config`` 設定可以讓 ``git log``, ``git show`` 增加上述風格的顯示格式::

	[core]
		abbrev = 12
	[pretty]
		fixes = Fixes: %h (\"%s\")

使用範例::

	$ git log -1 --pretty=fixes 54a4f0239f2e
	Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

.. _tw_split_changes:

拆分你的改動
------------

將每個 **邏輯更改** 拆分成一個單獨的補丁。

例如，如果你的改動裡同時有bug修正和效能最佳化，那麼把這些改動拆分到兩個或
者更多的補丁檔案中。如果你的改動包含對API的修改，並且增加了一個使用該新API
的驅動，那麼把這些修改分成兩個補丁。

另一方面，如果你將一個單獨的改動做成多個補丁檔案，那麼將它們合併成一個
單獨的補丁檔案。這樣一個邏輯上單獨的改動只被包含在一個補丁檔案裡。

需要記住的一點是，每個補丁的更改都應易於理解，以便審閱者驗證。每個補丁都應該
對其價值進行闡述。

如果有一個補丁依賴另外一個補丁來完成它的改動，那沒問題。直接在你的補丁
描述裡指出 **“這個補丁依賴某補丁”** 就好了。

在將您的更改劃分為一系列補丁時，要特別注意確保核心在應用系列中的每個補丁之後
都能正常建置和執行。使用 ``git bisect`` 來追蹤問題的開發者可能會在任何地方分
割你的補丁系列；如果你在中間引入錯誤，他們不會感謝你。

如果你不能將補丁系列濃縮得更小，那麼每次大約發送出15個補丁，然後等待審閱
和整合。

檢查你的更改風格
----------------

檢查您的補丁是否違反了基本樣式規定，詳細資訊參見
Documentation/translations/zh_TW/process/coding-style.rst
中找到。如果不這樣做，只會浪費審閱者的時間，並且會導致你的補丁被拒絕，甚至
可能沒有被閱讀。

一個重要的例外是在將程式碼從一個檔案移動到另一個檔案時——在這種情況下，您不
應該在移動程式碼的同一個補丁中修改移動的程式碼。這清楚地描述了移動程式碼和
您的更改的行為。這大大有助於審閱實際差異，並允許工具更好地追蹤程式碼本身的
歷史。

在提交之前，使用補丁樣式檢查程式檢查補丁（scripts/checkpatch.pl）。不過，
請注意，樣式檢查程式應該被視為一個指南，而不是作為人類判斷的替代品。如果您
的程式碼看起來更好，但有違規行為，那麼最好別管它。

檢查者報告三個級別：

 - ERROR：很可能出錯的事情
 - WARNING：需要仔細審閱的事項
 - CHECK：需要思考的事情

您應該能夠判斷您的補丁中存在的所有違規行為。

選擇補丁收件人
--------------

您應該總是知會任何補丁相應程式碼的子系統維護人員；查看
MAINTAINERS檔案和原始程式碼修訂歷史記錄，以瞭解這些維護人員是誰。腳本
scripts/get_maintainer.pl在這個步驟中非常有用。如果您找不到正在工作的子系統
的維護人員，那麼Andrew Morton（akpm@linux-foundation.org）將充當最後的維護
人員。

您通常還應該選擇至少一個郵件列表來接收補丁集的副本。linux-kernel@
vger.kernel.org是所有補丁的預設列表，但是這個列表的流量已經導致了許多開發
人員不再看它。在MAINTAINERS檔案中查找子系統特定的列表；您的補丁可能會在那
裡得到更多的關注。不過，請不要發送垃圾郵件到無關的列表。

許多與核心相關的列表託管在kernel.org上；您可以在
https://subspace.kernel.org 上找到它們的列表。不過，也有與核心相關
的列表託管在其他地方。

Linus Torvalds是決定改動能否進入 Linux 核心的最終裁決者。他的郵件地址是
torvalds@linux-foundation.org 。他收到的郵件很多，所以一般來說最好 **別**
給他發郵件。

如果您有修復可利用安全漏洞的補丁，請將該補丁發送到 security@kernel.org 。
對於嚴重的bug，可以考慮短期禁令以允許分銷商（有時間）向使用者發布補丁；在
這種情況下，顯然不應將補丁發送到任何公共列表。參見
Documentation/process/security-bugs.rst 。

修復已發布核心中嚴重錯誤的補丁程式應該抄送給穩定版維護人員，方法是把以下列行
放進補丁的簽署區（注意，不是電子郵件收件人）::

  Cc: stable@vger.kernel.org

除了本文件之外，您還應該閱讀
Documentation/translations/zh_TW/process/stable-kernel-rules.rst 。

如果更改影響到使用者側核心介面，請向手冊頁維護人員（如MAINTAINERS檔案中所
列）發送手冊頁補丁，或至少發送更改通知，以便一些資訊進入手冊頁。還應將使用
者空間API更改抄送到 linux-api@vger.kernel.org 。


不要MIME編碼，不要連結，不要壓縮，不要附件，只要純文字
------------------------------------------------------

Linus 和其他的核心開發者需要閱讀和評論你提交的改動。對於核心開發者來說
，可以“引用”你的改動很重要，使用一般的郵件工具，他們就可以在你的
程式碼的任何位置添加評論。

因為這個原因，所有的提交的補丁都是郵件中“內嵌”的。最簡單（和推薦）的方法就
是使用 ``git send-email`` 。https://git-send-email.io 有 ``git send-email``
的交互式教程。

如果你選擇不用 ``git send-email`` ：

.. warning::

  如果你使用剪切-貼上你的補丁，小心你的編輯器的自動換行功能破壞你的補丁

不要將補丁作為MIME編碼的附件，不管是否壓縮。很多流行的郵件軟體不
是任何時候都將MIME編碼的附件當作純文字發送的，這會使得別人無法在你的
程式碼中加評論。另外，MIME編碼的附件會讓Linus多花一點時間來處理，這就
降低了你的改動被接受的可能性。

例外：如果你的郵路損壞了補丁，那麼有人可能會要求你使用MIME重新發送補丁。

請參閱 Documentation/translations/zh_TW/process/email-clients.rst
以獲取有關設定電子郵件客戶端以使其不受影響地發送補丁的提示。

回覆審閱意見
------------

你的補丁幾乎肯定會得到審閱者對補丁改進方法的評論（以回覆郵件的形式）。您必須
對這些評論作出回應；讓補丁被忽略的一個好辦法就是忽略審閱者的意見。直接回復郵
件來回應意見即可。不會導致程式碼更改的意見或問題幾乎肯定會帶來註解或變更日誌的
改變，以便下一個審閱者更好地瞭解正在發生的事情。

一定要告訴審閱者你在做什麼改變，並感謝他們的時間。程式碼審閱是一個累人且耗時的
過程，審閱者有時會變得暴躁。即使在這種情況下，也要禮貌地回應並解決他們指出的
問題。當發送下一版時，在封面郵件或獨立補丁里加上 ``patch changelog`` 說明與
前一版本的不同之處（參見 :ref:`tw_the_canonical_patch_format` ）。

.. _tw_interleaved_replies:

在郵件討論中使用裁剪過的交錯式回覆
----------------------------------

在Linux核心開發的討論中，強烈不建議置頂回覆（top-posting）。交錯式（或
“行內”）回覆使對話更容易理解。更多細節參見：
https://en.wikipedia.org/wiki/Posting_style#Interleaved_style

正如郵件列表中經常被引用的那樣::

  A: http://en.wikipedia.org/wiki/Top_post
  Q: Where do I find info about this thing called top-posting?
  A: Because it messes up the order in which people normally read text.
  Q: Why is top-posting such a bad thing?
  A: Top-posting.
  Q: What is the most annoying thing in e-mail?

同樣，請裁剪掉所有與你的回覆無關的引文。這使回覆更容易查找，並節省時間和
空間。更多細節參見： http://daringfireball.net/2007/07/on_top ::

  A: No.
  Q: Should I include quotations after my reply?

.. _tw_resend_reminders:

不要泄氣或不耐煩
----------------

提交更改後，請耐心等待。審閱者是大忙人，可能無法立即審閱您的補丁。

曾幾何時，補丁曾在沒收到評論的情況下消失在虛空中，但現在開發過程應該更加順
利了。您應該在一週左右的時間內收到評論；如果沒有收到評論，請確保您已將補丁
發送到正確的位置。在重新提交或聯繫審閱者之前至少等待一週——在諸如合併視窗之
類的繁忙時間可能更長。

在等了幾個星期後，用帶RESEND的主題重發補丁也是可以的::

   [PATCH Vx RESEND] sub/sys: Condensed patch summary

當你發布補丁（系列）修改版的時候，不要加上“RESEND”——“RESEND”只適用於重
新提交之前未經修改的補丁（系列）。

主題中包含 PATCH
----------------

由於到Linus和linux-kernel的電子郵件流量很高，通常會在主題行前面加上[PATCH]
前綴。這使Linus和其他核心開發人員更容易將補丁與其他電子郵件討論區分開。

``git send-email`` 會自動為你加上。

簽署你的作品——開發者來源認證
------------------------------

為了加強對誰做了何事的追蹤，尤其是對那些透過好幾層維護者才最終到達的補丁，我
們在透過郵件發送的補丁上引入了“簽署（sign-off）”流程。

“簽署”是在補丁註解最後的一行簡單文字，認證你編寫了它或者其他
人有權力將它作為開放原始程式碼的補丁傳遞。規則很簡單：如果你能認證如下資訊:

開發者來源認證 1.1
^^^^^^^^^^^^^^^^^^

對於本專案的貢獻，我認證如下資訊：

       (a) 這些貢獻是完全或者部分的由我建立，我有權利以文件中指出
           的開放原始程式碼許可證提交它；或者

       (b) 這些貢獻基於以前的工作，據我所知，這些以前的工作受恰當的開放
           原始程式碼許可證保護，而且，根據文件中指出的許可證，我有權提交修改後的貢獻，
           無論是完全還是部分由我創造，這些貢獻都使用同一個開放原始程式碼許可證
           （除非我被允許用其它的許可證）；或者

       (c) 這些貢獻由認證（a），（b）或者（c）的人直接提供給我，而
           且我沒有修改它。

       (d) 我理解並同意這個專案和貢獻是公開的，貢獻的記錄（包括我
           一起提交的個人記錄，包括sign-off）被永久維護並且可以和這個專案
           或者開放原始程式碼的許可證同步地再發行。

那麼加入這樣一行::

  Signed-off-by: Random J Developer <random@developer.example.org>

使用你的真名（抱歉，不能使用假名或者匿名。）如果使用 ``git commit -s`` 的話
將會自動完成。撤銷也應當包含“Signed-off-by”， ``git revert -s`` 會幫你搞定。

有些人會在最後加上額外的標籤。現在這些東西會被忽略，但是你可以這樣做，來標記
公司內部的過程，或者只是指出關於簽署的一些特殊細節。

作者簽署之後的任何其他簽署（Signed-off-by:'s）均來自處理和傳遞補丁的人員，但
未參與其開發。簽署鏈應當反映補丁傳播到維護者並最終傳播到Linus所經過的 **真實**
路徑，首個簽署指明單個作者的主要作者身份。

何時使用Acked-by:，Cc:，和Co-developed-by:
------------------------------------------

Signed-off-by: 標籤表示簽名者參與了補丁的開發，或者他/她在補丁的傳遞路徑中。

如果一個人沒有直接參與補丁的準備或處理，但希望表示並記錄他們對補丁的批准/
贊成，那麼他們可以要求在補丁的變更日誌中添加一個Acked-by:。

Acked-by: 供以某種方式對受影響程式碼負責或與之相關的人使用。最常見的情況是，
當維護者既沒有貢獻也沒有轉發補丁時，由該維護者使用。

Acked-by: 也可以由其他利益相關者使用，例如具有領域知識的人（例如被修改程式
碼的原作者）、核心uAPI補丁的使用者空間側審閱者，或某項功能的關鍵使用者。在
這些情況下，可以視需要加上一個“# 後綴”以澄清其含義::

	Acked-by: The Stakeholder <stakeholder@example.org> # As primary user

Acked-by: 不像簽署那樣正式。這是一個記錄，確認人至少審閱了補丁，並表示接受。
因此，補丁合併有時會手動將Acker的“Yep，looks good to me”轉換為 Acked-By:（但
請注意，通常最好要求一個明確的Ack）。

Acked-by: 也不如 Reviewed-by: 正式。例如，維護者可以用它表示他們同意補丁
合入，但可能沒有像提供Reviewed-by:那樣徹底地審閱過補丁。同樣，關鍵使用者
可能沒有對補丁進行技術審閱，但他們可能對整體方法、功能或面向使用者的介面
感到滿意。

Acked-by：不一定表示對整個補丁的確認。例如，如果一個補丁影響多個子系統，並
且有一個來自某個子系統維護者的Acked-By:，那麼這通常表示只確認影響維護者程
式碼的部分。這裡應該仔細判斷。如有疑問，應參考郵件列表存檔中的原始討論。在
這種情況下也可以使用“# 後綴”來澄清。

如果某人本應有機會對補丁進行評論，但沒有提供此類評論，您可以選擇在補丁中添加
``Cc:`` 標籤。此標籤記錄了討論中包含的潛在利益相關方。注意，這是僅有的三個
可以在未經被指名者明確許可的情況下使用的標籤之一（詳見下面的“標記他人需要
許可”）。

Co-developed-by: 聲明補丁是由多個開發人員共同建立的；當幾個人在一個補丁上工
作時，它用於給出共同作者（除了From:所給出的作者之外）。因為Co-developed-by:
表示作者身份，所以每個Co-developed-by:必須緊跟在相關合作作者的簽署之後。標準
簽署程式要求Signed-off-by:標籤的順序應儘可能反映補丁的時間歷史，無論作者是通
過From:還是Co-developed-by:表明。值得注意的是，最後一個Signed-off-by:必須是
提交補丁的開發人員。

注意，如果From:作者也是電子郵件標題的From:行中列出的人，則From:標籤是可選的。

被From:作者提交的補丁範例::

	<changelog>

	Co-developed-by: First Co-Author <first@coauthor.example.org>
	Signed-off-by: First Co-Author <first@coauthor.example.org>
	Co-developed-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: From Author <from@author.example.org>

被合作開發者提交的補丁範例::

	From: From Author <from@author.example.org>

	<changelog>

	Co-developed-by: Random Co-Author <random@coauthor.example.org>
	Signed-off-by: Random Co-Author <random@coauthor.example.org>
	Signed-off-by: From Author <from@author.example.org>
	Co-developed-by: Submitting Co-Author <sub@coauthor.example.org>
	Signed-off-by: Submitting Co-Author <sub@coauthor.example.org>


使用Reported-by:、Tested-by:、Reviewed-by:、Suggested-by:和Fixes:
-----------------------------------------------------------------

Reported-by: 給那些發現錯誤並報告錯誤的人致謝，它希望激勵他們在將來再次幫助
我們。注意，Reported-by標籤是僅有的三個可以在未經被指名者明確許可的情況下
使用的標籤之一（詳見下面的“標記他人需要許可”）。此標籤是為Bug設計的；請不要
將其用於感謝功能請求。

Tested-by: 標籤表示補丁已由指定的人（在某些環境中）成功測試。這個標籤通知
維護人員已經執行了一些測試，為將來的補丁提供了一種定位測試人員的方法，並彰
顯測試人員的功勞。

Reviewed-by：根據審閱者的監督聲明，表明該補丁已被審閱並被認為是可接受的：


審閱者的監督聲明
^^^^^^^^^^^^^^^^

透過提供我的Reviewed-by:標籤，我聲明：

        (a) 我已經對這個補丁進行了一次技術審閱，以評估它是否適合被包含到
            主線核心中。

        (b) 與補丁相關的任何問題、顧慮或問題都已反饋給提交者。我對提交者對
            我的評論的回應感到滿意。

        (c) 雖然這一提交可能仍可被改進，但我相信，此時，（1）對核心
            進行了有價值的修改，（2）沒有包含爭論中涉及的已知問題。

        (d) 雖然我已經審閱了補丁並認為它是健全的，但我不會（除非另有明確
            說明）作出任何保證或擔保它會在任何給定情況下實作其規定的目的
            或正常執行。

Reviewed-by是一種觀點聲明，即補丁是對核心的適當修改，沒有任何遺留的嚴重技
術問題。任何感興趣的審閱者（完成了審閱工作且具有已知身分的人）都可以為一個
補丁提供一個Reviewed-by標籤。此標籤用於向審閱者提供致謝，並通知維護者補丁
的審閱進度。當Reviewed-by:標籤由已知了解主題區域並執行徹底檢查的審閱者提供
時，通常會增加補丁進入核心的可能性。

一旦從測試人員或審閱者的“Tested-by”和“Reviewed-by”標籤出現在郵件列表中，作
者應在發送下一個版本時將其添加到適用的補丁中。但是，如果補丁在以下版本中發
生了實質性更改，這些標籤可能不再適用，因此應該刪除。通常，刪除某人的
Acked-by、Tested-by或Reviewed-by標籤時，應在補丁更改日誌中（在 ``---`` 分
隔符之後）提及並附上解釋。

Suggested-by: 表示補丁的想法是由指定的人提出的，並確保將此想法歸功於指定的
人：如果我們勤快地致謝創意提供者，他們將受到鼓舞，很有希望在未來再次幫助
我們。注意，這是僅有的三個可以在未經被指名者明確許可的情況下使用的標籤之一
（詳見下面的“標記他人需要許可”）。

Fixes: 指示補丁修復了之前提交中的一個缺陷。它可以便於確定問題的來源，這有助於
檢查錯誤修復。這個標籤還幫助穩定核心團隊確定應該接收修復的穩定核心版本。這是
指示補丁修復的錯誤的首選方法。請參閱 :ref:`tw_describe_changes` 瞭解更多資訊。

.. note::

  附加Fixes:標籤不會改變穩定核心規則流程，也不改變所有穩定版補丁抄送
  stable@vger.kernel.org的要求。有關更多資訊，請閱讀
  Documentation/translations/zh_TW/process/stable-kernel-rules.rst 。

最後，雖然提供標籤是受歡迎的且通常非常受讚賞，但請注意，簽署者（即提交者和
維護者）可以自行斟酌是否採用所提供的標籤。

.. _tw_tagging_people:

標記他人需要許可
----------------

在補丁中添加上述標籤時要小心：除了Cc:、Reported-by:和Suggested-by:之外，
所有標籤都需要被指名者的明確許可。對於這三個標籤，如果根據lore存檔或提交
歷史，該人曾以該名字和電子郵件地址對Linux核心做出過貢獻，那麼隱含的許可
就足夠了——並且對於Reported-by:和Suggested-by:，報告或建議必須是公開作出
的。注意，就此而言bugzilla.kernel.org是公開場所，但其中使用的電子郵件地址
是私密的；因此不要在標籤中暴露它們，除非該人在先前的貢獻中使用過。

.. _tw_the_canonical_patch_format:

標準補丁格式
------------

本節描述如何格式化補丁本身。請注意，如果您的補丁儲存在 ``Git`` 儲存庫中，則
可以使用 ``git format-patch`` 進行正確的補丁格式化。但是，這些工具無法建立
必要的文字，因此請務必閱讀下面的說明。

主題行
^^^^^^

標準的補丁標題行是::

    Subject: [PATCH 001/123] 子系統:一句話概述

標準補丁的信體包含如下部分：

  - 一個 ``from`` 行指出補丁作者。後跟空行（僅當發送補丁的人不是作者時才需
    要）。

  - 說明文字，每行最長75列，這將被複制到永久變更日誌來描述這個補丁。

  - 一個空行

  - 上述的 ``Signed-off-by:`` 行，也將出現在更改日誌中。

  - 只包含 ``---`` 的標記線。

  - 任何其他不適合放在變更日誌的註解。

  - 實際補丁（ ``diff`` 輸出）。

標題行的格式，使得對標題行按字母序排序非常的容易——很多郵件客戶端都
可以支援——因為序列號是用零填充的，所以按數字排序和按字母排序是一樣的。

郵件標題中的“子系統”標識哪個核心子系統將被打補丁。

郵件標題中的“一句話概述”扼要的描述郵件中的補丁。“一句話概述”
不應該是一個檔名。對於一個補丁系列（“補丁系列”指一系列的多個相關補丁
），不要對每個補丁都使用同樣的“一句話概述”。

記住郵件的“一句話概述”會成為該補丁的全域唯一標識。它會進入 ``git``
的改動記錄裡。然後“一句話概述”會被用在開發者的討論裡，用來指代這個補丁
。使用者將希望透過搜索引擎搜索“一句話概述”來找到那些討論這個補丁的文
章。當人們在兩三個月後使用諸如 ``gitk`` 或 ``git log --oneline`` 之類
的工具查看數千個補丁時，也會很快看到它。

出於這些原因，概述必須不超過70-75個字元，並且必須描述補丁的更改以及為
什麼需要補丁。既要簡潔又要描述性很有挑戰性，但寫得好的概述應該這樣。

概述的前綴可以用方括號括起來：“Subject: [PATCH <tag>...] <概述>”。標記
不被視為概述的一部分，而是描述應該如何處理補丁。如果補丁的多個版本已發
送出來以響應評審（即“v1，v2，v3”）則必須包含版本號，或包含“RFC”以指示
評審請求。如果一個補丁系列中有四個補丁，那麼各個補丁可以這樣編號：1/4、2/4、
3/4、4/4。這可以確保開發人員瞭解補丁應用的順序，且
已經查看或應用了補丁系列中的所有補丁。

一些標題的例子::

    Subject: [PATCH 2/5] ext2: improve scalability of bitmap searching
    Subject: [PATCH v2 01/27] x86: fix eflags tracking
    Subject: [PATCH v2] sub/sys: Condensed patch summary
    Subject: [PATCH v2 M/N] sub/sys: Condensed patch summary

From行
^^^^^^

``From`` 行必須是信體裡的最上面一行，具有如下格式::

        From: Patch Author <author@example.com>

``From`` 行指明在永久改動日誌裡，誰會被確認為作者。如果沒有 ``From`` 行，那
麼郵件頭裡的 ``From:`` 行會被用來決定改動日誌中的作者。

作者可以透過在 ``from`` 行和 ``SoB`` 行中加上組織名稱，來表明其所屬單位
或工作的贊助者，例如：

	From: Patch Author (Company) <author@example.com>

說明主體
^^^^^^^^

說明文字將會被提交到永久的原始程式碼改動日誌裡，因此應針對那些早已經不記得和這
個補丁相關的討論細節的讀者。包括補丁處理的故障症狀（核心日誌訊息、oops訊息
等），這對於可能正在搜索提交日誌以查找適用補丁的人特別有用。文字應該寫得如
此詳細，以便在數週、數月甚至數年後閱讀時，能夠為讀者提供所需的細節資訊，以
掌握建立補丁的 **原因** 。

如果一個補丁修復了一個編譯失敗，那麼可能不需要包含 *所有* 編譯失敗；
只要足夠讓搜索補丁的人能夠找到它就行了。與概述一樣，既要簡潔又要描述性。


.. _tw_backtraces:

提交訊息中的回溯（Backtraces）
""""""""""""""""""""""""""""""

回溯有助於記錄導致問題的呼叫鏈。然而，並非所有回溯都有幫助。例如，早期引導呼
叫鏈是獨特而明顯的。而逐字複製完整的dmesg輸出則會增加時間戳、模組列表、暫存
器和堆疊轉儲等分散注意力的資訊。

因此，最有用的回溯應該從轉儲中提取相關資訊，以更容易集中在真實問題上。下面是
一個剪裁良好的回溯範例::

  unchecked MSR access error: WRMSR to 0xd51 (tried to write 0x0000000000000064)
  at rIP: 0xffffffffae059994 (native_write_msr+0x4/0x20)
  Call Trace:
  mba_wrmsr
  update_domains
  rdtgroup_mkdir

附加註解（Commentary）
^^^^^^^^^^^^^^^^^^^^^^

``---`` 標記行對於補丁處理工具要找到哪裡是改動日誌資訊的結束，是不可缺少
的。

對於 ``---`` 標記之後的額外註解，一個好的用途就是用來寫 ``diffstat`` ，用
來顯示修改了什麼檔案和每個檔案都增加和刪除了多少行。 ``diffstat`` 對於比較
大的補丁特別有用。使用 ``diffstat`` 的選項 ``-p 1 -w 70`` 這樣檔名就會從核
心原始程式碼樹的目錄開始，不會佔用太寬的空間（很容易適合80列的寬度，也許會
有一些縮排。）（ ``git`` 預設會產生合適的diffstat。）

其餘那些只適用於當時或者與維護者相關的註解，不合適放到永久的改動日誌裡的，也
應該放這裡。較好的例子就是 ``補丁更改記錄`` ，記錄了v1和v2版本補丁之間的差異。

請將此資訊放在將變更日誌與補丁的其餘部分分隔開的 ``---`` 行 **之後** 。版本
資訊不是提交到git樹的變更日誌的一部分。只是供審閱人員使用的附加資訊。如果將
其放置在提交標記上方，則需要手動交互才能將其刪除。如果它位於分隔線以下，則在
應用補丁時會自動剝離。如果可以，建議附上指向該補丁先前版本的連結（例如
lore.kernel.org存檔連結），以幫助審閱者::

  <commit message>
  ...
  Signed-off-by: Author <author@mail>
  ---
  V2 -> V3: Removed redundant helper function
  V1 -> V2: Cleaned up coding style and addressed review comments

  v2: https://lore.kernel.org/bar
  v1: https://lore.kernel.org/foo

  path/to/file | 5+++--
  ...

在後面的參考資料中能看到正確補丁格式的更多細節。


.. _tw_explicit_in_reply_to:

明確回覆郵件頭（In-Reply-To）
-----------------------------

手動添加回復補丁的的郵件頭（In-Reply_To:）是有用的（例如，使用 ``git send-email`` ），
可以將補丁與以前的相關討論關聯起來，例如，將bug補丁連結到電子郵件和bug報告。
但是，對於多補丁系列，最好避免在回覆時使用連結到該系列的舊版本。這樣，
補丁的多個版本就不會成為電子郵件客戶端中無法管理的引用樹。如果連結有用，
可以使用 https://lore.kernel.org/ 重定向器（例如，在封面電子郵件文字中）
連結到補丁系列的早期版本。

給出基礎樹資訊
--------------

當其他開發人員收到您的補丁並開始審閱時，知道應該將您的工作放到程式碼樹歷史記錄
中的什麼位置通常很有用。這對於自動化持續整合流水（CI）特別有用，這些流水線試
圖執行一系列測試，以便在維護人員開始審閱之前確定提交的品質。

如果您使用 ``git format-patch`` 產生補丁，則可以透過 ``--base`` 標誌在提交中
自動包含基礎樹資訊。使用此選項最簡單、最方便的方法是配合主題分支::

    $ git checkout -t -b my-topical-branch master
    Branch 'my-topical-branch' set up to track local branch 'master'.
    Switched to a new branch 'my-topical-branch'

    [perform your edits and commits]

    $ git format-patch --base=auto --cover-letter -o outgoing/ master
    outgoing/0000-cover-letter.patch
    outgoing/0001-First-Commit.patch
    outgoing/...

當你編輯 ``outgoing/0000-cover-letter.patch`` 時，您會注意到在它的最底部有一
行 ``base-commit:`` 尾註，它為審閱者和CI工具提供了足夠的資訊以正確執行
``git am`` 而不必擔心衝突::

    $ git checkout -b patch-review [base-commit-id]
    Switched to a new branch 'patch-review'
    $ git am patches.mbox
    Applying: First Commit
    Applying: ...

有關此選項的更多資訊，請參閱 ``man git-format-patch`` 。

.. note::

    ``--base`` 功能是在2.9.0版git中引入的。

如果您不使用git格式化補丁，仍然可以包含相同的 ``base-commit`` 尾註，以指示您
的工作所基於的樹的提交哈希。你應該在封面郵件或系列的第一個補丁中添加它，它應
該放在 ``---`` 行的下面或所有其他內容之後，即只在你的電子郵件簽名之前。

工具
----

此流程的許多技術層面都可以使用b4自動化，其說明文件見
<https://b4.docs.kernel.org/en/latest/>。它可以幫助追蹤依賴關係、執行
checkpatch，以及格式化和發送郵件。

參考文獻
--------

Andrew Morton，“完美的補丁”（tpp）
  <https://www.ozlabs.org/~akpm/stuff/tpp.txt>

Jeff Garzik，“Linux核心補丁提交格式”
  <https://web.archive.org/web/20180829112450/http://linux.yyz.us/patch-format.html>

Greg Kroah-Hartman，“如何惹惱核心子系統維護人員”
  <http://www.kroah.com/log/linux/maintainer.html>

  <http://www.kroah.com/log/linux/maintainer-02.html>

  <http://www.kroah.com/log/linux/maintainer-03.html>

  <http://www.kroah.com/log/linux/maintainer-04.html>

  <http://www.kroah.com/log/linux/maintainer-05.html>

  <http://www.kroah.com/log/linux/maintainer-06.html>

核心 Documentation/translations/zh_TW/process/coding-style.rst

Linus Torvalds關於標準補丁格式的郵件
  <https://lore.kernel.org/r/Pine.LNX.4.58.0504071023190.28951@ppc970.osdl.org>

Andi Kleen，“提交補丁之路”
  一些幫助合入困難或有爭議的變更的策略。

  http://halobates.de/on-submitting-patches.pdf

