.. SPDX-License-Identifier: GPL-2.0-or-later

.. include:: ../disclaimer-zh_TW.rst

.. _tw_email_clients:

:Original: Documentation/process/email-clients.rst

:譯者:
 - 賈威威  Harry Wei <harryxiyou@gmail.com>
 - 時奎亮  Alex Shi <alexs@kernel.org>
 - 吳想成  Wu XiangCheng <bobwxc@email.cn>

:校譯:
 - Yinglin Luan <synmyth@gmail.com>
 - Xiaochen Wang <wangxiaochen0@gmail.com>
 - yaxinsn <yaxinsn@163.com>
 - Hu Haowen <2023002089@link.tyut.edu.cn>

Linux郵件客戶端配置信息
=======================

Git
---

現在大多數開發人員使用 ``git send-email`` 而不是常規的電子郵件客戶端。這方面
的手冊非常好。在接收端，維護人員使用 ``git am`` 加載補丁。

如果你是 ``git`` 新手，那麼把你的第一個補丁發送給你自己。將其保存爲包含所有
標題的原始文本。運行 ``git am raw_email.txt`` ，然後使用 ``git log`` 查看更
改日誌。如果工作正常，再將補丁發送到相應的郵件列表。


通用配置
--------

Linux內核補丁是通過郵件被提交的，最好把補丁作爲郵件體的內嵌文本。有些維護者
接收附件，但是附件的內容格式應該是"text/plain"。然而，附件一般是不贊成的，
因爲這會使補丁的引用部分在評論過程中變的很困難。

同時也強烈建議在補丁或其他郵件的正文中使用純文本格式。https://useplaintext.email
有助於瞭解如何配置你喜歡的郵件客戶端，並在您還沒有首選的情況下列出一些推薦的
客戶端。

用來發送Linux內核補丁的郵件客戶端在發送補丁時應該處於文本的原始狀態。例如，
他們不能改變或者刪除製表符或者空格，甚至是在每一行的開頭或者結尾。

不要通過"format=flowed"模式發送補丁。這樣會引起不可預期以及有害的斷行。

不要讓你的郵件客戶端進行自動換行。這樣也會破壞你的補丁。

郵件客戶端不能改變文本的字符集編碼方式。要發送的補丁只能是ASCII或者UTF-8編碼
方式，如果你使用UTF-8編碼方式發送郵件，那麼你將會避免一些可能發生的字符集問題。

郵件客戶端應該生成並且保持“References:”或者“In-Reply-To:”郵件頭，這樣郵件會話
就不會中斷。

複製粘帖(或者剪貼粘帖)通常不能用於補丁，因爲製表符會轉換爲空格。使用xclipboard,
xclip或者xcutsel也許可以，但是最好測試一下或者避免使用複製粘帖。

不要在使用PGP/GPG簽名的郵件中包含補丁。這樣會使得很多腳本不能讀取和適用於你的
補丁。（這個問題應該是可以修復的）

在給內核郵件列表發送補丁之前，給自己發送一個補丁是個不錯的主意，保存接收到的
郵件，將補丁用'patch'命令打上，如果成功了，再給內核郵件列表發送。


一些郵件客戶端提示
------------------

這裏給出一些詳細的MUA配置提示，可以用於給Linux內核發送補丁。這些並不意味是
所有的軟件包配置總結。

說明：

- TUI = 以文本爲基礎的用戶接口
- GUI = 圖形界面用戶接口

Alpine (TUI)
************

配置選項：

在 :menuselection:`Sending Preferences` 菜單：

- :menuselection:`Do Not Send Flowed Text` 必須開啓
- :menuselection:`Strip Whitespace Before Sending` 必須關閉

當寫郵件時，光標應該放在補丁會出現的地方，然後按下 `CTRL-R` 組合鍵，使指
定的補丁文件嵌入到郵件中。

Claws Mail (GUI)
****************

可以用，有人用它成功地發過補丁。

用 :menuselection:`Message-->Insert File` (`CTRL-I`) 或外置編輯器插入補丁。

若要在Claws編輯窗口重修改插入的補丁，需關閉
:menuselection:`Configuration-->Preferences-->Compose-->Wrapping`
的 `Auto wrapping` 。

Evolution (GUI)
***************

一些開發者成功的使用它發送補丁。

撰寫郵件時：
從 :menuselection:`格式-->段落樣式-->預格式化` (`CTRL-7`)
或工具欄選擇 :menuselection:`預格式化` ；

然後使用：
:menuselection:`插入-->文本文件...` (`ALT-N x`) 插入補丁文件。

你還可以 ``diff -Nru old.c new.c | xclip`` ，選擇 :menuselection:`預格式化` ，
然後使用鼠標中鍵進行粘帖。

Kmail (GUI)
***********

一些開發者成功的使用它發送補丁。

默認撰寫設置禁用HTML格式是合適的；不要啓用它。

當書寫一封郵件的時候，在選項下面不要選擇自動換行。唯一的缺點就是你在郵件中輸
入的任何文本都不會被自動換行，因此你必須在發送補丁之前手動換行。最簡單的方法
就是啓用自動換行來書寫郵件，然後把它保存爲草稿。一旦你在草稿中再次打開它，它
已經全部自動換行了，那麼你的郵件雖然沒有選擇自動換行，但是還不會失去已有的自
動換行。

在郵件的底部，插入補丁之前，放上常用的補丁定界符：三個連字符(``---``)。

然後在 :menuselection:`信件` 菜單，選擇 :menuselection:`插入文本文件` ，接
着選取你的補丁文件。還有一個額外的選項，你可以通過它配置你的創建新郵件工具欄，
加上 :menuselection:`插入文本文件` 圖標。

將編輯器窗口拉到足夠寬避免折行。對於KMail 1.13.5 (KDE 4.5.4)，它會在發送郵件
時對編輯器窗口中顯示折行的地方自動換行。在選項菜單中取消自動換行仍不能解決。
因此，如果你的補丁中有非常長的行，必須在發送之前把編輯器窗口拉得非常寬。
參見：https://bugs.kde.org/show_bug.cgi?id=174034

你可以安全地用GPG簽名附件，但是內嵌補丁最好不要使用GPG簽名它們。作爲內嵌文本
插入的簽名補丁將使其難以從7-bit編碼中提取。

如果你非要以附件的形式發送補丁，那麼就右鍵點擊附件，然後選擇
:menuselection:`屬性` ，打開 :menuselection:`建議自動顯示` ，使附件內聯更容
易讓讀者看到。

當你要保存將要發送的內嵌文本補丁，你可以從消息列表窗格選擇包含補丁的郵件，然
後右鍵選擇 :menuselection:`另存爲` 。如果整個電子郵件的組成正確，您可直接將
其作爲補丁使用。電子郵件以當前用戶可讀寫權限保存，因此您必須 ``chmod`` ，以
使其在複製到別處時用戶組和其他人可讀。

Lotus Notes (GUI)
*****************

不要使用它。

IBM Verse (Web GUI)
*******************

同上條。

Mutt (TUI)
**********

很多Linux開發人員使用mutt客戶端，這證明它肯定工作得非常漂亮。

Mutt不自帶編輯器，所以不管你使用什麼編輯器，不自動斷行就行。大多數編輯器都有
:menuselection:`插入文件` 選項，它可以在不改變文件內容的情況下插入文件。

用 ``vim`` 作爲mutt的編輯器::

  set editor="vi"

如果使用xclip，敲入以下命令::

  :set paste

然後再按中鍵或者shift-insert或者使用::

  :r filename

把補丁插入爲內嵌文本。
在未設置  ``set paste`` 時(a)ttach工作的很好。

你可以通過 ``git format-patch`` 生成補丁，然後用 Mutt發送它們::

    $ mutt -H 0001-some-bug-fix.patch

配置選項：

它應該以默認設置的形式工作。
然而，把 ``send_charset`` 設置一下也是一個不錯的主意::

  set send_charset="us-ascii:utf-8"

Mutt 是高度可配置的。 這裏是個使用mutt通過 Gmail 發送的補丁的最小配置::

  # .muttrc
  # ================  IMAP ====================
  set imap_user = 'yourusername@gmail.com'
  set imap_pass = 'yourpassword'
  set spoolfile = imaps://imap.gmail.com/INBOX
  set folder = imaps://imap.gmail.com/
  set record="imaps://imap.gmail.com/[Gmail]/Sent Mail"
  set postponed="imaps://imap.gmail.com/[Gmail]/Drafts"
  set mbox="imaps://imap.gmail.com/[Gmail]/All Mail"

  # ================  SMTP  ====================
  set smtp_url = "smtp://username@smtp.gmail.com:587/"
  set smtp_pass = $imap_pass
  set ssl_force_tls = yes # Require encrypted connection

  # ================  Composition  ====================
  set editor = `echo \$EDITOR`
  set edit_headers = yes  # See the headers when editing
  set charset = UTF-8     # value of $LANG; also fallback for send_charset
  # Sender, email address, and sign-off line must match
  unset use_domain        # because joe@localhost is just embarrassing
  set realname = "YOUR NAME"
  set from = "username@gmail.com"
  set use_from = yes

Mutt文檔含有更多信息：

    https://gitlab.com/muttmua/mutt/-/wikis/UseCases/Gmail

    http://www.mutt.org/doc/manual/

Pine (TUI)
**********

Pine過去有一些空格刪減問題，但是這些現在應該都被修復了。

如果可以，請使用alpine（pine的繼承者）。

配置選項：

- 最近的版本需要 ``quell-flowed-text``
- ``no-strip-whitespace-before-send`` 選項也是需要的。


Sylpheed (GUI)
**************

- 內嵌文本可以很好的工作（或者使用附件）。
- 允許使用外部的編輯器。
- 收件箱較多時非常慢。
- 如果通過non-SSL連接，無法使用TLS SMTP授權。
- 撰寫窗口的標尺很有用。
- 將地址添加到通訊簿時無法正確理解顯示的名稱。

Thunderbird (GUI)
*****************

Thunderbird是Outlook的克隆版本，它很容易損壞文本，但也有一些方法強制修正。

在完成修改後（包括安裝擴展），您需要重新啓動Thunderbird。

- 允許使用外部編輯器：

  使用Thunderbird發補丁最簡單的方法是使用擴展來打開您最喜歡的外部編輯器。

  下面是一些能夠做到這一點的擴展樣例。

  - “External Editor Revived”

    https://github.com/Frederick888/external-editor-revived

    https://addons.thunderbird.net/en-GB/thunderbird/addon/external-editor-revived/

    它需要安裝“本地消息主機（native messaging host）”。
    參見以下文檔:
    https://github.com/Frederick888/external-editor-revived/wiki

  - “External Editor”

    https://github.com/exteditor/exteditor

    下載並安裝此擴展，然後打開 :menuselection:`新建消息` 窗口, 用
    :menuselection:`查看-->工具欄-->自定義...` 給它增加一個按鈕，直接點擊此
    按鈕即可使用外置編輯器。

    請注意，“External Editor”要求你的編輯器不能fork，換句話說，編輯器必須在
    關閉前不返回。你可能需要傳遞額外的參數或修改編輯器設置。最值得注意的是，
    如果您使用的是gvim，那麼您必須將 :menuselection:`external editor` 設置的
    編輯器字段設置爲 ``/usr/bin/gvim --nofork"`` （假設可執行文件在
    ``/usr/bin`` ），以傳遞 ``-f`` 參數。如果您正在使用其他編輯器，請閱讀其
    手冊瞭解如何處理。

若要修正內部編輯器，請執行以下操作：

- 修改你的Thunderbird設置，不要使用 ``format=flowed`` ！
  回到主窗口，按照
  :menuselection:`主菜單-->首選項-->常規-->配置編輯器...`
  打開Thunderbird的配置編輯器。

  - 將 ``mailnews.send_plaintext_flowed`` 設爲 ``false``

  - 將 ``mailnews.wraplength`` 從 ``72`` 改爲 ``0``

- 不要寫HTML郵件！
  回到主窗口，打開
  :menuselection:`主菜單-->賬戶設置-->你的@郵件.地址-->通訊錄/編寫&地址簿` ，
  關掉 ``以HTML格式編寫消息`` 。

- 只用純文本格式查看郵件！
  回到主窗口， :menuselection:`主菜單-->查看-->消息體爲-->純文本` ！

TkRat (GUI)
***********

可以使用它。使用"Insert file..."或者外部的編輯器。

Gmail (Web GUI)
***************

不要使用它發送補丁。

Gmail網頁客戶端自動地把製表符轉換爲空格。

雖然製表符轉換爲空格問題可以被外部編輯器解決，但它同時還會使用回車換行把每行
拆分爲78個字符。

另一個問題是Gmail還會把任何含有非ASCII的字符的消息改用base64編碼，如歐洲人的
名字。


