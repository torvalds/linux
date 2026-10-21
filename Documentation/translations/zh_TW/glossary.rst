.. SPDX-License-Identifier: GPL-2.0

.. _tw_glossary:

術語表
======

:作者: 葉宸佑 Chen-Yu Yeh <chenyou910331@gmail.com>

本術語表列出Linux核心繁體中文（zh_TW）翻譯所使用的術語。zh_TW翻譯以臺灣
慣用的資訊術語為準。新增或更新翻譯時，請依照本表選擇用詞，以維持整個
zh_TW文件樹的一致性；若遇到本表未收錄的術語，歡迎提交補丁補充。

下表同時列出常見的簡體中文（zh_CN）譯法，方便從zh_CN翻譯轉換或對照時
使用。本表的zh_CN欄位為對照參考，其用詞經核對核心文件樹zh_CN譯文中的
實際用法，非引用外部術語表。

一般術語
--------

========================  ==========================  ====================
英文                      zh_TW                       zh_CN（對照）
========================  ==========================  ====================
kernel                    核心                        内核
user / user space         使用者／使用者空間          用户／用户空间
software / hardware       軟體／硬體                  软件／硬件
firmware                  韌體                        固件
operating system          作業系統                    操作系统
distribution              發行版                      发行版
community                 社群                        社区
project                   專案                        项目
documentation             文件                        文档
file                      檔案                        文件
folder / directory        資料夾／目錄                文件夹／目录
default                   預設                        默认／缺省
support                   支援                        支持
information               資訊                        信息
message                   訊息                        消息
data                      資料                        数据
quality                   品質                        质量
performance               效能                        性能
network                   網路                        网络
the Internet              網際網路                    因特网
server                    伺服器                      服务器
digital                   數位                        数字
computer                  電腦                        计算机
integrate                 整合                        集成
create                    建立                        创建
access                    存取                        访问
via / through             透過                        通过
========================  ==========================  ====================

程式設計術語
------------

========================  ==========================  ====================
英文                      zh_TW                       zh_CN（對照）
========================  ==========================  ====================
source code               原始程式碼／原始碼          源代码
code                      程式碼                      代码
program                   程式                        程序
process                   行程                        进程
thread                    執行緒                      线程
scheduler / schedule      排程器／排程                调度器／调度
queue                     佇列                        队列
memory                    記憶體                      内存
cache                     快取                        缓存
interface                 介面                        接口
port                      埠                          端口
register                  暫存器                      寄存器
stack                     堆疊                        堆栈
function                  函式                        函数
function call             呼叫                        调用
callback                  回呼                        回调
return value              回傳值                      返回值
macro                     巨集                        宏
enum                      列舉                        枚举
variable                  變數                        变量
pointer                   指標                        指针
array                     陣列                        数组
linked list               鏈結串列                    链表
loop                      迴圈                        循环
declaration               宣告                        声明
identifier                識別字                      标识符
character / string        字元／字串                  字符／字符串
byte                      位元組                      字节
boolean                   布林                        布尔
binary                    二進位                      二进制
object                    物件                        对象
type                      型別／類型                  类型
module                    模組                        模块
component                 元件                        组件
build                     建置                        构建
compile / assembler       編譯／組譯器                编译／汇编器
debug                     除錯                        调试
optimize                  最佳化                      优化
implement                 實作                        实现
global / local            全域／區域                  全局／局部
constant                  常數                        常量
comment (in code)         註解                        注释
indentation               縮排                        缩进
inline                    行內                        内联
generate                  產生                        生成
load / loader             載入／載入器                加载／加载器
export / import           匯出／匯入                  导出／导入
link / linker             連結／連結器                链接／链接器
repository                儲存庫                      存储库／仓库
distributed               分散式                      分布式
header file               標頭檔                      头文件
configuration file        設定檔                      配置文件
file system               檔案系統                    文件系统
settings / configuration  設定                        设置／配置
========================  ==========================  ====================

介面與其他術語
--------------

========================  ==========================  ====================
英文                      zh_TW                       zh_CN（對照）
========================  ==========================  ====================
menu                      選單                        菜单
window                    視窗                        窗口
toolbar                   工具列                      工具栏
field                     欄位                        字段
icon                      圖示                        图标
mouse / cursor            滑鼠／游標                  鼠标／光标
paste                     貼上                        粘贴
print / printer           列印／印表機                打印／打印机
disk (hard disk)          磁碟（硬碟）                磁盘（硬盘）
text / plain text         文字／純文字                文本／纯文本
mailbox                   信箱                        邮箱
account                   帳戶／帳號                  帐户
tracking                  追蹤                        跟踪
advanced                  進階                        高级
free software             自由軟體                    自由软件／免费软件
========================  ==========================  ====================

特定用語說明
------------

- **access 與 visit**：access（資料、記憶體、資源的存取）譯為「存取」；
  visit（造訪網站、頁面或連結）譯為「造訪」。兩者在 zh_CN 皆作「訪問」，
  zh_TW 依語意區分。

- **Fellow**：現有翻譯將 Linux Foundation Fellow 譯為「院士」。「院士」在
  臺灣多指中央研究院等學術機構的頭銜，「研究員」也不完全對等，實務上亦常
  直接保留英文。此譯法尚在討論中，暫維持現有譯法，待社群取得共識後再統一。

- **mailing list**：譯為「郵件列表」（zh_CN 亦作「邮件列表」）。

- **-stable 與穩定版**：核心的 ``-stable`` 樹保留英文原名 ``-stable``；泛指
  時譯為「穩定版核心」。既有翻譯中「穩定版」與 ``-stable`` 混用者不強制
  統一，新增或更新的翻譯請套用此慣例。

用字慣例
--------

除術語之外，zh_TW翻譯採用臺灣標準用字，例如「為」（不用「爲」）、「裡」
（不用「裏」）、「著」（不用「着」）、「才」（不用「纔」）、「啟」（不用
「啓」）、「只」（不用「隻」）與「發布」（不用「發佈」）。

標點符號沿用現有文件樹的全形標點。每行寬度沿用原文慣例（每行約75個半形
字元寬）。

中文與英文、數字相鄰時不加空格，例如「Linux核心」而非「Linux 核心」；此為
現有zh_TW文件樹的主要慣例。本規則適用於新增的翻譯與改寫過的段落，既有檔案
不另行回頭修改。行內標記（``literal``、:ref: 等）前後仍依reStructuredText
的需要保留必要的空白。
