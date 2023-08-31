.. SPDX-License-Identifier: GPL-2.0

.. _tw_email_clients:

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/email-clients.rst <email_clients>`

譯者::

        中文版維護者： 賈威威  Harry Wei <harryxiyou@gmail.com>
        中文版翻譯者： 賈威威  Harry Wei <harryxiyou@gmail.com>
                       時奎亮  Alex Shi <alex.shi@linux.alibaba.com>
        中文版校譯者： Yinglin Luan <synmyth@gmail.com>
        	       Xiaochen Wang <wangxiaochen0@gmail.com>
                       yaxinsn <yaxinsn@163.com>
                      Hu Haowen <src.res.211@gmail.com>

Linux郵件客戶端配置信息
=======================

Git
---

現在大多數開發人員使用 ``git send-email`` 而不是常規的電子郵件客戶端。這方面
的手冊非常好。在接收端，維護人員使用 ``git am`` 加載補丁。

如果你是 ``git`` 新手，那麼把你的第一個補丁發送給你自己。將其保存爲包含所有
標題的原始文本。運行 ``git am raw_email.txt`` ，然後使用 ``git log`` 查看更
改日誌。如果工作正常，再將補丁發送到相應的郵件列表。


普通配置
--------
Linux內核補丁是通過郵件被提交的，最好把補丁作爲郵件體的內嵌文本。有些維護者
接收附件，但是附件的內容格式應該是"text/plain"。然而，附件一般是不贊成的，
因爲這會使補丁的引用部分在評論過程中變的很困難。

用來發送Linux內核補丁的郵件客戶端在發送補丁時應該處於文本的原始狀態。例如，
他們不能改變或者刪除制表符或者空格，甚至是在每一行的開頭或者結尾。

不要通過"format=flowed"模式發送補丁。這樣會引起不可預期以及有害的斷行。

不要讓你的郵件客戶端進行自動換行。這樣也會破壞你的補丁。

郵件客戶端不能改變文本的字符集編碼方式。要發送的補丁只能是ASCII或者UTF-8編碼方式，
如果你使用UTF-8編碼方式發送郵件，那麼你將會避免一些可能發生的字符集問題。

郵件客戶端應該形成並且保持 References: 或者 In-Reply-To: 標題，那麼
郵件話題就不會中斷。

複製粘帖(或者剪貼粘帖)通常不能用於補丁，因爲制表符會轉換爲空格。使用xclipboard, xclip
或者xcutsel也許可以，但是最好測試一下或者避免使用複製粘帖。

不要在使用PGP/GPG署名的郵件中包含補丁。這樣會使得很多腳本不能讀取和適用於你的補丁。
（這個問題應該是可以修復的）

在給內核郵件列表發送補丁之前，給自己發送一個補丁是個不錯的主意，保存接收到的
郵件，將補丁用'patch'命令打上，如果成功了，再給內核郵件列表發送。


一些郵件客戶端提示
------------------
這裡給出一些詳細的MUA配置提示，可以用於給Linux內核發送補丁。這些並不意味是
所有的軟體包配置總結。

說明：
TUI = 以文本爲基礎的用戶接口
GUI = 圖形界面用戶接口

Alpine (TUI)
~~~~~~~~~~~~

配置選項：
在"Sending Preferences"部分：

- "Do Not Send Flowed Text"必須開啓
- "Strip Whitespace Before Sending"必須關閉

當寫郵件時，光標應該放在補丁會出現的地方，然後按下CTRL-R組合鍵，使指定的
補丁文件嵌入到郵件中。

Evolution (GUI)
~~~~~~~~~~~~~~~

一些開發者成功的使用它發送補丁

當選擇郵件選項：Preformat
  從Format->Heading->Preformatted (Ctrl-7)或者工具欄

然後使用：
  Insert->Text File... (Alt-n x)插入補丁文件。

你還可以"diff -Nru old.c new.c | xclip"，選擇Preformat，然後使用中間鍵進行粘帖。

Kmail (GUI)
~~~~~~~~~~~

一些開發者成功的使用它發送補丁。

默認設置不爲HTML格式是合適的；不要啓用它。

當書寫一封郵件的時候，在選項下面不要選擇自動換行。唯一的缺點就是你在郵件中輸入的任何文本
都不會被自動換行，因此你必須在發送補丁之前手動換行。最簡單的方法就是啓用自動換行來書寫郵件，
然後把它保存爲草稿。一旦你在草稿中再次打開它，它已經全部自動換行了，那麼你的郵件雖然沒有
選擇自動換行，但是還不會失去已有的自動換行。

在郵件的底部，插入補丁之前，放上常用的補丁定界符：三個連字號(---)。

然後在"Message"菜單條目，選擇插入文件，接著選取你的補丁文件。還有一個額外的選項，你可以
通過它配置你的郵件建立工具欄菜單，還可以帶上"insert file"圖標。

你可以安全地通過GPG標記附件，但是內嵌補丁最好不要使用GPG標記它們。作爲內嵌文本的簽發補丁，
當從GPG中提取7位編碼時會使他們變的更加複雜。

如果你非要以附件的形式發送補丁，那麼就右鍵點擊附件，然後選中屬性，突出"Suggest automatic
display"，這樣內嵌附件更容易讓讀者看到。

當你要保存將要發送的內嵌文本補丁，你可以從消息列表窗格選擇包含補丁的郵件，然後右擊選擇
"save as"。你可以使用一個沒有更改的包含補丁的郵件，如果它是以正確的形式組成。當你正真在它
自己的窗口之下察看，那時沒有選項可以保存郵件--已經有一個這樣的bug被匯報到了kmail的bugzilla
並且希望這將會被處理。郵件是以只針對某個用戶可讀寫的權限被保存的，所以如果你想把郵件複製到其他地方，
你不得不把他們的權限改爲組或者整體可讀。

Lotus Notes (GUI)
~~~~~~~~~~~~~~~~~

不要使用它。

Mutt (TUI)
~~~~~~~~~~

很多Linux開發人員使用mutt客戶端，所以證明它肯定工作的非常漂亮。

Mutt不自帶編輯器，所以不管你使用什麼編輯器都不應該帶有自動斷行。大多數編輯器都帶有
一個"insert file"選項，它可以通過不改變文件內容的方式插入文件。

'vim'作爲mutt的編輯器：
  set editor="vi"

  如果使用xclip，敲入以下命令
  :set paste
  按中鍵之前或者shift-insert或者使用
  :r filename

如果想要把補丁作爲內嵌文本。
(a)ttach工作的很好，不帶有"set paste"。

你可以通過 ``git format-patch`` 生成補丁，然後用 Mutt發送它們::

        $ mutt -H 0001-some-bug-fix.patch

配置選項：
它應該以默認設置的形式工作。
然而，把"send_charset"設置爲"us-ascii::utf-8"也是一個不錯的主意。

Mutt 是高度可配置的。 這裡是個使用mutt通過 Gmail 發送的補丁的最小配置::

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

Mutt文檔含有更多信息:

    http://dev.mutt.org/trac/wiki/UseCases/Gmail

    http://dev.mutt.org/doc/manual.html

Pine (TUI)
~~~~~~~~~~

Pine過去有一些空格刪減問題，但是這些現在應該都被修復了。

如果可以，請使用alpine(pine的繼承者)

配置選項：
- 最近的版本需要消除流程文本
- "no-strip-whitespace-before-send"選項也是需要的。


Sylpheed (GUI)
~~~~~~~~~~~~~~

- 內嵌文本可以很好的工作（或者使用附件）。
- 允許使用外部的編輯器。
- 對於目錄較多時非常慢。
- 如果通過non-SSL連接，無法使用TLS SMTP授權。
- 在組成窗口中有一個很有用的ruler bar。
- 給地址本中添加地址就不會正確的了解顯示名。

Thunderbird (GUI)
~~~~~~~~~~~~~~~~~

默認情況下，thunderbird很容易損壞文本，但是還有一些方法可以強制它變得更好。

- 在用戶帳號設置里，組成和尋址，不要選擇"Compose messages in HTML format"。

- 編輯你的Thunderbird配置設置來使它不要拆行使用：user_pref("mailnews.wraplength", 0);

- 編輯你的Thunderbird配置設置，使它不要使用"format=flowed"格式：user_pref("mailnews.
  send_plaintext_flowed", false);

- 你需要使Thunderbird變爲預先格式方式：
  如果默認情況下你書寫的是HTML格式，那不是很難。僅僅從標題欄的下拉框中選擇"Preformat"格式。
  如果默認情況下你書寫的是文本格式，你不得把它改爲HTML格式（僅僅作爲一次性的）來書寫新的消息，
  然後強制使它回到文本格式，否則它就會拆行。要實現它，在寫信的圖標上使用shift鍵來使它變爲HTML
  格式，然後標題欄的下拉框中選擇"Preformat"格式。

- 允許使用外部的編輯器：
  針對Thunderbird打補丁最簡單的方法就是使用一個"external editor"擴展，然後使用你最喜歡的
  $EDITOR來讀取或者合併補丁到文本中。要實現它，可以下載並且安裝這個擴展，然後添加一個使用它的
  按鍵View->Toolbars->Customize...最後當你書寫信息的時候僅僅點擊它就可以了。

TkRat (GUI)
~~~~~~~~~~~

可以使用它。使用"Insert file..."或者外部的編輯器。

Gmail (Web GUI)
~~~~~~~~~~~~~~~

不要使用它發送補丁。

Gmail網頁客戶端自動地把制表符轉換爲空格。

雖然制表符轉換爲空格問題可以被外部編輯器解決，同時它還會使用回車換行把每行拆分爲78個字符。

另一個問題是Gmail還會把任何不是ASCII的字符的信息改爲base64編碼。它把東西變的像歐洲人的名字。

                                ###

