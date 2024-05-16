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


提交補丁：如何讓你的改動進入內核
================================

對於想要將改動提交到 Linux 內核的個人或者公司來說，如果不熟悉“規矩”，
提交的流程會讓人畏懼。本文檔包含了一系列建議，可以大大提高你
的改動被接受的機會.

本文檔以較爲簡潔的行文給出了大量建議。關於內核開發流程如何進行的詳細信息，
參見： Documentation/translations/zh_CN/process/development-process.rst 。
Documentation/translations/zh_CN/process/submit-checklist.rst 給出了一系列
提交補丁之前要檢查的事項。設備樹相關的補丁，請參閱
Documentation/devicetree/bindings/submitting-patches.rst 。

本文檔假設您正在使用 ``git`` 準備你的補丁。如果您不熟悉 ``git`` ，最好學習
如何使用它，這將使您作爲內核開發人員的生活變得更加輕鬆。

部分子系統和維護人員的樹有一些關於其工作流程和要求的額外信息，請參閱
Documentation/process/maintainer-handbooks.rst 。

獲取當前源碼樹
--------------

如果您手頭沒有當前內核源代碼的存儲庫，請使用 ``git`` 獲取一份。您需要先獲取
主線存儲庫，它可以通過以下命令拉取::

    git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

但是，請注意，您可能不想直接針對主線樹進行開發。大多數子系統維護人員運
行自己的樹，並希望看到針對這些樹準備的補丁。請參見MAINTAINERS文件中子系
統的 **T:** 項以查找該樹，或者直接詢問維護者該樹是否未在其中列出。

.. _tw_describe_changes:

描述你的改動
------------

描述你的問題。無論您的補丁是一行錯誤修復還是5000行新功能，都必須有一個潛在
的問題激勵您完成這項工作。說服審閱者相信有一個問題值得解決，讓他們讀完第一段
後就能明白這一點。

描述用戶可見的影響。直接崩潰和鎖定是相當有說服力的，但並不是所有的錯誤都那麼
明目張膽。即使在代碼審閱期間發現了這個問題，也要描述一下您認爲它可能對用戶產
生的影響。請記住，大多數Linux安裝運行的內核來自二級穩定樹或特定於供應商/產品
的樹，只從上游精選特定的補丁，因此請包含任何可以幫助您將更改定位到下游的內容：
觸發的場景、DMESG的摘錄、崩潰描述、性能迴歸、延遲尖峯、鎖定等。

質量優化和權衡。如果您聲稱在性能、內存消耗、堆棧佔用空間或二進制大小方面有所
改進，請包括支持它們的數據。但也要描述不明顯的成本。優化通常不是零成本的，而是
在CPU、內存和可讀性之間進行權衡；或者，做探索性的工作，在不同的工作負載之間進
行權衡。請描述優化的預期缺點，以便審閱者可以權衡成本和收益。

提出問題之後，就要詳細地描述一下您實際在做的技術細節。對於審閱者來說，用簡練的
英語描述代碼的變化是很重要的，以驗證代碼的行爲是否符合您的意圖。

如果您將補丁描述寫成“標準格式”，可以很容易地作爲“提交日誌”放入Linux的源代
碼管理系統 ``git`` 中，那麼維護人員將非常感謝您。
參見 :ref:`zh_the_canonical_patch_format` 。

每個補丁只解決一個問題。如果你的描述開始變長，這就表明你可能需要拆分你的補丁。
請見 :ref:`zh_split_changes` 。

提交或重新提交補丁或補丁系列時，請包括完整的補丁說明和理由。不要
只說這是補丁（系列）的第幾版。不要期望子系統維護人員引用更早的補丁版本或引用
URL來查找補丁描述並將其放入補丁中。也就是說，補丁（系列）及其描述應該是獨立的。
這對維護人員和審閱者都有好處。一些審閱者可能甚至沒有收到補丁的早期版本。

用祈使句描述你的變更，例如“make xyzzy do frotz”而不是“[This patch]make
xyzzy do frotz”或“[I]changed xyzzy to do frotz”，就好像你在命令代碼庫改變
它的行爲一樣。

如果您想要引用一個特定的提交，不要只引用提交的SHA-1 ID。還請包括提交的一行
摘要，以便於審閱者瞭解它是關於什麼的。例如::

        Commit e21d2170f36602ae2708 ("video: remove unnecessary
        platform_set_drvdata()") removed the unnecessary
        platform_set_drvdata(), but left the variable "dev" unused,
        delete it.

您還應該確保至少使用前12位SHA-1 ID。內核存儲庫包含 *許多* 對象，使較短的ID
發生衝突的可能性很大。記住，即使現在不會與您的六個字符ID發生衝突，這種情況
也可能在五年後改變。

如果該變更的相關討論或背景信息可以在網上查閱，請加上“Link:”標籤指向它。例如
你的補丁修復了一個缺陷，需要添加一個帶有URL的標籤指向郵件列表存檔或缺陷跟蹤器
的相關報告；如果該補丁是由一些早先郵件列表討論或網絡上的記錄引起的，請指向它。

當鏈接到郵件列表存檔時，請首選lore.kernel.org郵件存檔服務。用郵件中的
``Message-ID`` 頭（去掉尖括號）可以創建鏈接URL。例如::

    Link: https://lore.kernel.org/r/30th.anniversary.repost@klaava.Helsinki.FI/

請檢查該鏈接以確保可用且指向正確的郵件。

不過，在沒有外部資源的情況下，也要儘量讓你的解釋可理解。除了提供郵件列表存檔或
缺陷的URL之外，還要需要總結該補丁的相關討論要點。

如果補丁修復了特定提交中的錯誤，例如使用 ``git bisct`` 發現了一個問題，請使用
帶有前12個字符SHA-1 ID的“Fixes:”標籤和單行摘要。爲了簡化解析腳本，不要將該
標籤拆分爲多行，標籤不受“75列換行”規則的限制。例如::

  Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

下列 ``git config`` 設置可以讓 ``git log``, ``git show`` 增加上述風格的顯示格式::

	[core]
		abbrev = 12
	[pretty]
		fixes = Fixes: %h (\"%s\")

使用示例::

	$ git log -1 --pretty=fixes 54a4f0239f2e
	Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

.. _tw_split_changes:

拆分你的改動
------------

將每個 **邏輯更改** 拆分成一個單獨的補丁。

例如，如果你的改動裏同時有bug修正和性能優化，那麼把這些改動拆分到兩個或
者更多的補丁文件中。如果你的改動包含對API的修改，並且增加了一個使用該新API
的驅動，那麼把這些修改分成兩個補丁。

另一方面，如果你將一個單獨的改動做成多個補丁文件，那麼將它們合併成一個
單獨的補丁文件。這樣一個邏輯上單獨的改動只被包含在一個補丁文件裏。

需要記住的一點是，每個補丁的更改都應易於理解，以便審閱者驗證。每個補丁都應該
對其價值進行闡述。

如果有一個補丁依賴另外一個補丁來完成它的改動，那沒問題。直接在你的補
丁描述裏指出 **“這個補丁依賴某補丁”** 就好了。

在將您的更改劃分爲一系列補丁時，要特別注意確保內核在應用系列中的每個補丁之後
都能正常構建和運行。使用 ``git bisect`` 來追蹤問題的開發者可能會在任何地方分
割你的補丁系列；如果你在中間引入錯誤，他們不會感謝你。

如果你不能將補丁系列濃縮得更小，那麼每次大約發送出15個補丁，然後等待審閱
和集成。

檢查你的更改風格
----------------

檢查您的補丁是否違反了基本樣式規定，詳細信息參見
Documentation/translations/zh_CN/process/coding-style.rst
中找到。如果不這樣做，只會浪費審閱者的時間，並且會導致你的補丁被拒絕，甚至
可能沒有被閱讀。

一個重要的例外是在將代碼從一個文件移動到另一個文件時——在這種情況下，您不應
該在移動代碼的同一個補丁中修改移動的代碼。這清楚地描述了移動代碼和您的更改
的行爲。這大大有助於審閱實際差異，並允許工具更好地跟蹤代碼本身的歷史。

在提交之前，使用補丁樣式檢查程序檢查補丁（scripts/check patch.pl）。不過，
請注意，樣式檢查程序應該被視爲一個指南，而不是作爲人類判斷的替代品。如果您
的代碼看起來更好，但有違規行爲，那麼最好別管它。

檢查者報告三個級別：

 - ERROR：很可能出錯的事情
 - WARNING：需要仔細審閱的事項
 - CHECK：需要思考的事情

您應該能夠判斷您的補丁中存在的所有違規行爲。

選擇補丁收件人
--------------

您應該總是知會任何補丁相應代碼的子系統維護人員；查看
維護人員文件和源代碼修訂歷史記錄，以瞭解這些維護人員是誰。腳本
scripts/get_maintainer.pl在這個步驟中非常有用。如果您找不到正在工作的子系統
的維護人員，那麼Andrew Morton（akpm@linux-foundation.org）將充當最後的維護
人員。

您通常還應該選擇至少一個郵件列表來接收補丁集的副本。linux-kernel@vger.kernel.org
是所有補丁的默認列表，但是這個列表的流量已經導致了許多開發人員不再看它。
在MAINTAINERS文件中查找子系統特定的列表；您的補丁可能會在那裏得到更多的關注。
不過，請不要發送垃圾郵件到無關的列表。

許多與內核相關的列表託管在vger.kernel.org上；您可以在
http://vger.kernel.org/vger-lists.html 上找到它們的列表。不過，也有與內核相關
的列表託管在其他地方。

不要一次發送超過15個補丁到vger郵件列表！！！！

Linus Torvalds是決定改動能否進入 Linux 內核的最終裁決者。他的郵件地址是
torvalds@linux-foundation.org 。他收到的郵件很多，所以一般來說最好 **別**
給他發郵件。

如果您有修復可利用安全漏洞的補丁，請將該補丁發送到 security@kernel.org 。對於
嚴重的bug，可以考慮短期禁令以允許分銷商（有時間）向用戶發佈補丁；在這種情況下，
顯然不應將補丁發送到任何公共列表。
參見 Documentation/translations/zh_CN/admin-guide/security-bugs.rst 。

修復已發佈內核中嚴重錯誤的補丁程序應該抄送給穩定版維護人員，方法是把以下列行
放進補丁的籤準區（注意，不是電子郵件收件人）::

  Cc: stable@vger.kernel.org

除了本文件之外，您還應該閱讀
Documentation/translations/zh_CN/process/stable-kernel-rules.rst 。

如果更改影響到用戶側內核接口，請向手冊頁維護人員（如維護人員文件中所列）發送
手冊頁補丁，或至少發送更改通知，以便一些信息進入手冊頁。還應將用戶空間API
更改抄送到 linux-api@vger.kernel.org 。


不要MIME編碼，不要鏈接，不要壓縮，不要附件，只要純文本
------------------------------------------------------

Linus 和其他的內核開發者需要閱讀和評論你提交的改動。對於內核開發者來說
，可以“引用”你的改動很重要，使用一般的郵件工具，他們就可以在你的
代碼的任何位置添加評論。

因爲這個原因，所有的提交的補丁都是郵件中“內嵌”的。最簡單（和推薦）的方法就
是使用 ``git send-email`` 。https://git-send-email.io 有 ``git send-email``
的交互式教程。

如果你選擇不用 ``git send-email`` ：

.. warning::

  如果你使用剪切-粘貼你的補丁，小心你的編輯器的自動換行功能破壞你的補丁

不要將補丁作爲MIME編碼的附件，不管是否壓縮。很多流行的郵件軟件不
是任何時候都將MIME編碼的附件當作純文本發送的，這會使得別人無法在你的
代碼中加評論。另外，MIME編碼的附件會讓Linus多花一點時間來處理，這就
降低了你的改動被接受的可能性。

例外：如果你的郵路損壞了補丁，那麼有人可能會要求你使用MIME重新發送補丁。

請參閱 Documentation/translations/zh_CN/process/email-clients.rst
以獲取有關配置電子郵件客戶端以使其不受影響地發送補丁的提示。

回覆審閱意見
------------

你的補丁幾乎肯定會得到審閱者對補丁改進方法的評論（以回覆郵件的形式）。您必須
對這些評論作出回應；讓補丁被忽略的一個好辦法就是忽略審閱者的意見。直接回復郵
件來回應意見即可。不會導致代碼更改的意見或問題幾乎肯定會帶來註釋或變更日誌的
改變，以便下一個審閱者更好地瞭解正在發生的事情。

一定要告訴審閱者你在做什麼改變，並感謝他們的時間。代碼審閱是一個累人且耗時的
過程，審閱者有時會變得暴躁。即使在這種情況下，也要禮貌地回應並解決他們指出的
問題。當發送下一版時，在封面郵件或獨立補丁里加上 ``patch changelog`` 說明與
前一版本的不同之處（參見 :ref:`zh_the_canonical_patch_format` ）。

.. _tw_resend_reminders:

不要泄氣或不耐煩
----------------

提交更改後，請耐心等待。審閱者是大忙人，可能無法立即審閱您的補丁。

曾幾何時，補丁曾在沒收到評論的情況下消失在虛空中，但現在開發過程應該更加順利了。
您應該在一週左右的時間內收到評論；如果沒有收到評論，請確保您已將補丁發送
到正確的位置。在重新提交或聯繫審閱者之前至少等待一週——在諸如合併窗口之類的
繁忙時間可能更長。

在等了幾個星期後，用帶RESEND的主題重發補丁也是可以的::

   [PATCH Vx RESEND] sub/sys: Condensed patch summary

當你發佈補丁（系列）修改版的時候，不要加上“RESEND”——“RESEND”只適用於重
新提交之前未經修改的補丁（系列）。

主題中包含 PATCH
----------------

由於到Linus和linux-kernel的電子郵件流量很高，通常會在主題行前面加上[PATCH]
前綴。這使Linus和其他內核開發人員更容易將補丁與其他電子郵件討論區分開。

``git send-email`` 會自動爲你加上。

簽署你的作品——開發者來源認證
------------------------------

爲了加強對誰做了何事的追蹤，尤其是對那些透過好幾層維護者才最終到達的補丁，我
們在通過郵件發送的補丁上引入了“簽署（sign-off）”流程。

“簽署”是在補丁註釋最後的一行簡單文字，認證你編寫了它或者其他
人有權力將它作爲開放源代碼的補丁傳遞。規則很簡單：如果你能認證如下信息:

開發者來源認證 1.1
^^^^^^^^^^^^^^^^^^

對於本項目的貢獻，我認證如下信息：

       (a) 這些貢獻是完全或者部分的由我創建，我有權利以文件中指出
           的開放源代碼許可證提交它；或者

       (b) 這些貢獻基於以前的工作，據我所知，這些以前的工作受恰當的開放
           源代碼許可證保護，而且，根據文件中指出的許可證，我有權提交修改後的貢獻，
           無論是完全還是部分由我創造，這些貢獻都使用同一個開放源代碼許可證
           （除非我被允許用其它的許可證）；或者

       (c) 這些貢獻由認證（a），（b）或者（c）的人直接提供給我，而
           且我沒有修改它。

       (d) 我理解並同意這個項目和貢獻是公開的，貢獻的記錄（包括我
           一起提交的個人記錄，包括sign-off）被永久維護並且可以和這個項目
           或者開放源代碼的許可證同步地再發行。

那麼加入這樣一行::

  Signed-off-by: Random J Developer <random@developer.example.org>

使用你的真名（抱歉，不能使用假名或者匿名。）如果使用 ``git commit -s`` 的話
將會自動完成。撤銷也應當包含“Signed-off-by”， ``git revert -s`` 會幫你搞定。

有些人會在最後加上額外的標籤。現在這些東西會被忽略，但是你可以這樣做，來標記
公司內部的過程，或者只是指出關於簽署的一些特殊細節。

作者簽署之後的任何其他簽署（Signed-off-by:'s）均來自處理和傳遞補丁的人員，但
未參與其開發。簽署鏈應當反映補丁傳播到維護者並最終傳播到Linus所經過的 **真實**
路徑，首個簽署指明單個作者的主要作者身份。

何時使用Acked-by:，CC:，和Co-Developed by:
------------------------------------------

Singed-off-by: 標籤表示簽名者參與了補丁的開發，或者他/她在補丁的傳遞路徑中。

如果一個人沒有直接參與補丁的準備或處理，但希望表示並記錄他們對補丁的批准/贊成，
那麼他們可以要求在補丁的變更日誌中添加一個Acked-by:。

Acked-by: 通常由受影響代碼的維護者使用，當該維護者既沒有貢獻也沒有轉發補丁時。

Acked-by: 不像簽署那樣正式。這是一個記錄，確認人至少審閱了補丁，並表示接受。
因此，補丁合併有時會手動將Acker的“Yep，looks good to me”轉換爲 Acked-By:（但
請注意，通常最好要求一個明確的Ack）。

Acked-by：不一定表示對整個補丁的確認。例如，如果一個補丁影響多個子系統，並且
有一個來自某個子系統維護者的Acked-By:，那麼這通常表示只確認影響維護者代碼的部
分。這裏應該仔細判斷。如有疑問，應參考郵件列表存檔中的原始討論。

如果某人本應有機會對補丁進行評論，但沒有提供此類評論，您可以選擇在補丁中添加
``Cc:`` 這是唯一可以在沒有被該人明確同意的情況下添加的標籤——但它應該表明
這個人是在補丁上抄送的。此標籤記錄了討論中包含的潛在利益相關方。

Co-developed-by: 聲明補丁是由多個開發人員共同創建的；當幾個人在一個補丁上工
作時，它用於給出共同作者（除了From:所給出的作者之外）。因爲Co-developed-by:
表示作者身份，所以每個Co-developed-by:必須緊跟在相關合作作者的簽署之後。標準
簽署程序要求Singed-off-by:標籤的順序應儘可能反映補丁的時間歷史，無論作者是通
過From:還是Co-developed-by:表明。值得注意的是，最後一個Singed-off-by:必須是
提交補丁的開發人員。

注意，如果From:作者也是電子郵件標題的From:行中列出的人，則From:標籤是可選的。

被From:作者提交的補丁示例::

	<changelog>

	Co-developed-by: First Co-Author <first@coauthor.example.org>
	Signed-off-by: First Co-Author <first@coauthor.example.org>
	Co-developed-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: From Author <from@author.example.org>

被合作開發者提交的補丁示例::

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
我們。請注意，如果bug是以私有方式報告的，那麼在使用Reported-by標籤之前，請
先請求許可。此標籤是爲Bug設計的；請不要將其用於感謝功能請求。

Tested-by: 標籤表示補丁已由指定的人（在某些環境中）成功測試。這個標籤通知
維護人員已經執行了一些測試，爲將來的補丁提供了一種定位測試人員的方法，並彰顯測試人員的功勞。

Reviewed-by：根據審閱者的監督聲明，表明該補丁已被審閱並被認爲是可接受的：


審閱者的監督聲明
^^^^^^^^^^^^^^^^

通過提供我的Reviewed-by:標籤，我聲明：

        (a) 我已經對這個補丁進行了一次技術審閱，以評估它是否適合被包含到
            主線內核中。

        (b) 與補丁相關的任何問題、顧慮或問題都已反饋給提交者。我對提交者對
            我的評論的回應感到滿意。

        (c) 雖然這一提交可能仍可被改進，但我相信，此時，（1）對內核
            進行了有價值的修改，（2）沒有包含爭論中涉及的已知問題。

        (d) 雖然我已經審閱了補丁並認爲它是健全的，但我不會（除非另有明確
            說明）作出任何保證或擔保它會在任何給定情況下實現其規定的目的
            或正常運行。

Reviewed-by是一種觀點聲明，即補丁是對內核的適當修改，沒有任何遺留的嚴重技術
問題。任何感興趣的審閱者（完成工作的人）都可以爲一個補丁提供一個Reviewed-by
標籤。此標籤用於向審閱者提供致謝，並通知維護者補丁的審閱進度。
當Reviewed-by:標籤由已知了解主題區域並執行徹底檢查的審閱者提供時，通常會增加
補丁進入內核的可能性。

一旦從測試人員或審閱者的“Tested-by”和“Reviewed-by”標籤出現在郵件列表中，
作者應在發送下一個版本時將其添加到適用的補丁中。但是，如果補丁在以下版本中發
生了實質性更改，這些標籤可能不再適用，因此應該刪除。通常，在補丁更改日誌中
（在 ``---`` 分隔符之後）應該提到刪除某人的測試者或審閱者標籤。

Suggested-by: 表示補丁的想法是由指定的人提出的，並確保將此想法歸功於指定的
人。請注意，未經許可，不得添加此標籤，特別是如果該想法未在公共論壇上發佈。
也就是說，如果我們勤快地致謝創意提供者，他們將受到鼓舞，很有希望在未來再次
幫助我們。

Fixes: 指示補丁修復了之前提交的一個問題。它可以便於確定錯誤的來源，這有助於
檢查錯誤修復。這個標籤還幫助穩定內核團隊確定應該接收修復的穩定內核版本。這是
指示補丁修復的錯誤的首選方法。請參閱 :ref:`zh_describe_changes` 瞭解更多信息。

.. note::

  附加Fixes:標籤不會改變穩定內核規則流程，也不改變所有穩定版補丁抄送
  stable@vger.kernel.org的要求。有關更多信息，請閱讀
  Documentation/translations/zh_CN/process/stable-kernel-rules.rst 。

.. _tw_the_canonical_patch_format:

標準補丁格式
------------

本節描述如何格式化補丁本身。請注意，如果您的補丁存儲在 ``Git`` 存儲庫中，則
可以使用 ``git format-patch`` 進行正確的補丁格式化。但是，這些工具無法創建
必要的文本，因此請務必閱讀下面的說明。

標準的補丁標題行是::

    Subject: [PATCH 001/123] 子系統:一句話概述

標準補丁的信體包含如下部分：

  - 一個 ``from`` 行指出補丁作者。後跟空行（僅當發送補丁的人不是作者時才需要）。

  - 說明文字，每行最長75列，這將被複制到永久變更日誌來描述這個補丁。

  - 一個空行

  - 上述的 ``Signed-off-by:`` 行，也將出現在更改日誌中。

  - 只包含 ``---`` 的標記線。

  - 任何其他不適合放在變更日誌的註釋。

  - 實際補丁（ ``diff`` 輸出）。

標題行的格式，使得對標題行按字母序排序非常的容易——很多郵件客戶端都
可以支持——因爲序列號是用零填充的，所以按數字排序和按字母排序是一樣的。

郵件標題中的“子系統”標識哪個內核子系統將被打補丁。

郵件標題中的“一句話概述”扼要的描述郵件中的補丁。“一句話概述”
不應該是一個文件名。對於一個補丁系列（“補丁系列”指一系列的多個相關補
丁），不要對每個補丁都使用同樣的“一句話概述”。

記住郵件的“一句話概述”會成爲該補丁的全局唯一標識。它會進入 ``git``
的改動記錄裏。然後“一句話概述”會被用在開發者的討論裏，用來指代這個補
丁。用戶將希望通過搜索引擎搜索“一句話概述”來找到那些討論這個補丁的文
章。當人們在兩三個月後使用諸如 ``gitk`` 或 ``git log --oneline`` 之類
的工具查看數千個補丁時，也會很快看到它。

出於這些原因，概述必須不超過70-75個字符，並且必須描述補丁的更改以及爲
什麼需要補丁。既要簡潔又要描述性很有挑戰性，但寫得好的概述應該這樣。

概述的前綴可以用方括號括起來：“Subject: [PATCH <tag>...] <概述>”。標記
不被視爲概述的一部分，而是描述應該如何處理補丁。如果補丁的多個版本已發
送出來以響應評審（即“v1，v2，v3”）則必須包含版本號，或包含“RFC”以指示
評審請求。如果一個補丁系列中有四個補丁，那麼各個補丁可以這樣編號：1/4、2/4、
3/4、4/4。這可以確保開發人員瞭解補丁應用的順序，且
已經查看或應用了補丁系列中的所有補丁。

一些標題的例子::

    Subject: [patch 2/5] ext2: improve scalability of bitmap searching
    Subject: [PATCHv2 001/207] x86: fix eflags tracking

``From`` 行是信體裏的最上面一行，具有如下格式::

        From: Patch Author <author@example.com>

``From`` 行指明在永久改動日誌裏，誰會被確認爲作者。如果沒有 ``From`` 行，那
麼郵件頭裏的 ``From:`` 行會被用來決定改動日誌中的作者。

說明文字將會被提交到永久的源代碼改動日誌裏，因此應針對那些早已經不記得和這
個補丁相關的討論細節的讀者。包括補丁處理的故障症狀（內核日誌消息、oops消息
等），這對於可能正在搜索提交日誌以查找適用補丁的人特別有用。文本應該寫得如
此詳細，以便在數週、數月甚至數年後閱讀時，能夠爲讀者提供所需的細節信息，以
掌握創建補丁的 **原因** 。

如果一個補丁修復了一個編譯失敗，那麼可能不需要包含 *所有* 編譯失敗；
只要足夠讓搜索補丁的人能夠找到它就行了。與概述一樣，既要簡潔又要描述性。

``---`` 標記行對於補丁處理工具要找到哪裏是改動日誌信息的結束，是不可缺少
的。

對於 ``---`` 標記之後的額外註解，一個好的用途就是用來寫 ``diffstat`` ，用來顯
示修改了什麼文件和每個文件都增加和刪除了多少行。 ``diffstat`` 對於比較大的補
丁特別有用。
使用 ``diffstat`` 的選項 ``-p 1 -w 70`` 這樣文件名就會從內核源代碼樹的目錄開始
，不會佔用太寬的空間（很容易適合80列的寬度，也許會有一些縮進。）
（ ``git`` 默認會生成合適的diffstat。）

其餘那些只適用於當時或者與維護者相關的註解，不合適放到永久的改動日誌裏的，也
應該放這裏。較好的例子就是 ``補丁更改記錄`` ，記錄了v1和v2版本補丁之間的差異。

請將此信息放在將變更日誌與補丁的其餘部分分隔開的 ``---`` 行 **之後** 。版本
信息不是提交到git樹的變更日誌的一部分。只是供審閱人員使用的附加信息。如果將
其放置在提交標記上方，則需要手動交互才能將其刪除。如果它位於分隔線以下，則在
應用補丁時會自動剝離::

  <commit message>
  ...
  Signed-off-by: Author <author@mail>
  ---
  V2 -> V3: Removed redundant helper function
  V1 -> V2: Cleaned up coding style and addressed review comments

  path/to/file | 5+++--
  ...

在後面的參考資料中能看到正確補丁格式的更多細節。

.. _tw_backtraces:

提交消息中的回溯（Backtraces）
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

回溯有助於記錄導致問題的調用鏈。然而，並非所有回溯都有幫助。例如，早期引導調
用鏈是獨特而明顯的。而逐字複製完整的dmesg輸出則會增加時間戳、模塊列表、寄存
器和堆棧轉儲等分散注意力的信息。

因此，最有用的回溯應該從轉儲中提取相關信息，以更容易集中在真實問題上。下面是
一個剪裁良好的回溯示例::

  unchecked MSR access error: WRMSR to 0xd51 (tried to write 0x0000000000000064)
  at rIP: 0xffffffffae059994 (native_write_msr+0x4/0x20)
  Call Trace:
  mba_wrmsr
  update_domains
  rdtgroup_mkdir

.. _tw_explicit_in_reply_to:

明確回覆郵件頭（In-Reply-To）
-----------------------------

手動添加回復補丁的的郵件頭（In-Reply_To:）是有用的（例如，使用 ``git send-email`` ），
可以將補丁與以前的相關討論關聯起來，例如，將bug補丁鏈接到電子郵件和bug報告。
但是，對於多補丁系列，最好避免在回覆時使用鏈接到該系列的舊版本。這樣，
補丁的多個版本就不會成爲電子郵件客戶端中無法管理的引用樹。如果鏈接有用，
可以使用 https://lore.kernel.org/ 重定向器（例如，在封面電子郵件文本中）
鏈接到補丁系列的早期版本。

給出基礎樹信息
--------------

當其他開發人員收到您的補丁並開始審閱時，知道應該將您的工作放到代碼樹歷史記錄
中的什麼位置通常很有用。這對於自動化持續集成流水（CI）特別有用，這些流水線試
圖運行一系列測試，以便在維護人員開始審閱之前確定提交的質量。

如果您使用 ``git format-patch`` 生成補丁，則可以通過 ``--base`` 標誌在提交中
自動包含基礎樹信息。使用此選項最簡單、最方便的方法是配合主題分支::

    $ git checkout -t -b my-topical-branch master
    Branch 'my-topical-branch' set up to track local branch 'master'.
    Switched to a new branch 'my-topical-branch'

    [perform your edits and commits]

    $ git format-patch --base=auto --cover-letter -o outgoing/ master
    outgoing/0000-cover-letter.patch
    outgoing/0001-First-Commit.patch
    outgoing/...

當你編輯 ``outgoing/0000-cover-letter.patch`` 時，您會注意到在它的最底部有一
行 ``base-commit:`` 尾註，它爲審閱者和CI工具提供了足夠的信息以正確執行
``git am`` 而不必擔心衝突::

    $ git checkout -b patch-review [base-commit-id]
    Switched to a new branch 'patch-review'
    $ git am patches.mbox
    Applying: First Commit
    Applying: ...

有關此選項的更多信息，請參閱 ``man git-format-patch`` 。

.. note::

    ``--base`` 功能是在2.9.0版git中引入的。

如果您不使用git格式化補丁，仍然可以包含相同的 ``base-commit`` 尾註，以指示您
的工作所基於的樹的提交哈希。你應該在封面郵件或系列的第一個補丁中添加它，它應
該放在 ``---`` 行的下面或所有其他內容之後，即只在你的電子郵件簽名之前。

參考文獻
--------

Andrew Morton，“完美的補丁”（tpp）
  <https://www.ozlabs.org/~akpm/stuff/tpp.txt>

Jeff Garzik，“Linux內核補丁提交格式”
  <https://web.archive.org/web/20180829112450/http://linux.yyz.us/patch-format.html>

Greg Kroah-Hartman，“如何惹惱內核子系統維護人員”
  <http://www.kroah.com/log/linux/maintainer.html>

  <http://www.kroah.com/log/linux/maintainer-02.html>

  <http://www.kroah.com/log/linux/maintainer-03.html>

  <http://www.kroah.com/log/linux/maintainer-04.html>

  <http://www.kroah.com/log/linux/maintainer-05.html>

  <http://www.kroah.com/log/linux/maintainer-06.html>

不！！！別再發巨型補丁炸彈給linux-kernel@vger.kernel.org的人們了！
  <https://lore.kernel.org/r/20050711.125305.08322243.davem@davemloft.net>

內核 Documentation/translations/zh_CN/process/coding-style.rst

Linus Torvalds關於標準補丁格式的郵件
  <https://lore.kernel.org/r/Pine.LNX.4.58.0504071023190.28951@ppc970.osdl.org>

Andi Kleen，“提交補丁之路”
  一些幫助合入困難或有爭議的變更的策略。

  http://halobates.de/on-submitting-patches.pdf

