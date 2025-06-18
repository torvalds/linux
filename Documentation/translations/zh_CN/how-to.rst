.. SPDX-License-Identifier: GPL-2.0

==========================
Linux 内核中文文档翻译规范
==========================

修订记录：
 - v1.0 2025 年 3 月 28 日，司延腾、慕冬亮共同编写了该规范。

制定规范的背景
==============

过去几年，在广大社区爱好者的友好合作下，Linux 内核中文文档迎来了蓬勃的发
展。在翻译的早期，一切都是混乱的，社区对译稿只有一个准确翻译的要求，以鼓
励更多的开发者参与进来，这是从 0 到 1 的必然过程，所以早期的中文文档目录
更加具有多样性，不过好在文档不多，维护上并没有过大的压力。

然而，世事变幻，不觉有年，现在内核中文文档在前进的道路上越走越远，很多潜
在的问题逐渐浮出水面，而且随着中文文档数量的增加，翻译更多的文档与提高中
文文档可维护性之间的矛盾愈发尖锐。由于文档翻译的特殊性，很多开发者并不会
一直更新文档，如果中文文档落后英文文档太多，文档更新的工作量会远大于重新
翻译。而且邮件列表中陆续有新的面孔出现，他们那股热情，就像燃烧的火焰，能
瞬间点燃整个空间，可是他们的补丁往往具有个性，这会给审阅带来了很大的困难，
reviewer 们只能耐心地指导他们如何与社区更好地合作，但是这项工作具有重复
性，长此以往，会渐渐浇灭 reviewer 审阅的热情。

虽然内核文档中已经有了类似的贡献指南，但是缺乏专门针对于中文翻译的，尤其
是对于新手来说，浏览大量的文档反而更加迷惑，该文档就是为了缓解这一问题而
编写，目的是为提供给新手一个快速翻译指南。

详细的贡献指南：Documentation/translations/zh_CN/process/index.rst。

环境搭建
========

工欲善其事必先利其器，如果您目前对内核文档翻译满怀热情，并且会独立地安装
Linux 发行版和简单地使用 Linux 命令行，那么可以迅速开始了。若您尚不具备该
能力，很多网站上会有详细的手把手教程，最多一个上午，您应该就能掌握对应技
能。您需要注意的一点是，请不要使用 root 用户进行后续步骤和文档翻译。

拉取开发树
----------

中文文档翻译工作目前独立于 linux-doc 开发树开展，所以您需要拉取该开发树，
打开终端命令行执行::

	git clone git://git.kernel.org/pub/scm/linux/kernel/git/alexs/linux.git

如果您遇到网络连接问题，也可以执行以下命令::

	git clone https://mirrors.hust.edu.cn/git/kernel-doc-zh.git linux

这是 Alex 开发树的镜像库，每两个小时同步一次上游。如果您了解到更快的 mirror，
请随时 **添加** 。

命令执行完毕后，您会在当前目录下得到一个 linux 目录，该目录就是您之后的工作
仓库，请把它放在一个稳妥的位置。

安装文档构建环境
----------------

内核仓库里面提供了一个半自动化脚本，执行该脚本，会检测您的发行版中需要安
装哪些软件包，请按照命令行提示进行安装，通常您只需要复制命令并执行就行。
::

	cd linux
	./scripts/sphinx-pre-install

以 Fedora 为例，它的输出是这样的::

	You should run:

		sudo dnf install -y dejavu-sans-fonts dejavu-sans-mono-fonts \
		     dejavu-serif-fonts google-noto-sans-cjk-fonts graphviz-gd \
	             latexmk librsvg2-tools texlive-anyfontsize texlive-capt-of \
		     texlive-collection-fontsrecommended texlive-ctex \
		     texlive-eqparbox texlive-fncychap texlive-framed \
		     texlive-luatex85 texlive-multirow texlive-needspace \
		     texlive-tabulary texlive-threeparttable texlive-upquote \
		     texlive-wrapfig texlive-xecjk

	Sphinx needs to be installed either:
	1) via pip/pypi with:

		/usr/bin/python3 -m venv sphinx_latest
		. sphinx_latest/bin/activate
		pip install -r ./Documentation/sphinx/requirements.txt

	    If you want to exit the virtualenv, you can use:
		deactivate

	2) As a package with:

		sudo dnf install -y python3-sphinx

	    Please note that Sphinx >= 3.0 will currently produce false-positive
	   warning when the same name is used for more than one type (functions,
	   structs, enums,...). This is known Sphinx bug. For more details, see:
		https://github.com/sphinx-doc/sphinx/pull/8313

请您按照提示复制打印的命令到命令行执行，您必须具备 root 权限才能执行 sudo
开头的命令。**请注意**，最新版本 Sphinx 的文档编译速度有极大提升，强烈建议
您通过 pip/pypi 安装最新版本 Sphinx。

如果您处于一个多用户环境中，为了避免对其他人造成影响，建议您配置单用户
sphinx 虚拟环境，即只需要执行::

	/usr/bin/python3 -m venv sphinx_latest
	. sphinx_latest/bin/activate
	pip install -r ./Documentation/sphinx/requirements.txt

最后执行以下命令退出虚拟环境::

	deactivate

您可以在任何需要的时候再次执行以下命令进入虚拟环境::

	. sphinx_latest/bin/activate

进行第一次文档编译
------------------

进入开发树目录::

	cd linux

这是一个标准的编译和调试流程，请每次构建时都严格执行::

	. sphinx_latest/bin/activate
	make cleandocs
	make htmldocs
	deactivate

检查编译结果
------------

编译输出在 Documentation/output/ 目录下，请用浏览器打开该目录下对应
的文件进行检查。

Git 和邮箱配置
--------------

打开命令行执行::

	sudo dnf install git-email
	vim ~/.gitconfig

这里是我的一个配置文件示范，请根据您的邮箱域名服务商提供的手册替换到对
应的字段。
::

	[user]
	       name = Yanteng Si		# 这会出现在您的补丁头部签名栏
	       email = si.yanteng@linux.dev	# 这会出现在您的补丁头部签名栏

	[sendemail]
	       from = Yanteng Si <si.yanteng@linux.dev>	# 这会出现在您的补丁头部
	       smtpencryption = ssl
	       smtpserver = smtp.migadu.com
	       smtpuser = si.yanteng@linux.dev
	       smtppass = <passwd>      	# 建议使用第三方客户端专用密码
	       chainreplyto = false
	       smtpserverport = 465

关于邮件客户端的配置，请查阅 Documentation/translations/zh_CN/process/email-clients.rst。

开始翻译文档
============

文档索引结构
------------

目前中文文档是在 Documentation/translations/zh_CN/ 目录下进行，该
目录结构最终会与 Documentation/ 结构一致，所以您只需要将您感兴趣的英文
文档文件和对应的 index.rst 复制到 zh_CN 目录下对应的位置，然后修改更
上一级的 index 即可开始您的翻译。

为了保证翻译的文档补丁被顺利合并，不建议多人同时翻译一个目录，因为这会
造成补丁之间互相依赖，往往会导致一部分补丁被合并，另一部分产生冲突。

如果实在无法避免两个人同时对一个目录进行翻译的情况，请将补丁制作进一个补
丁集。但是不推荐刚开始就这么做，因为经过实践，在没有指导的情况下，新手很
难一次处理好这个补丁集。

请执行以下命令，新建开发分支::

	git checkout docs-next
	git checkout -b my-trans

译文格式要求
------------

	- 每行长度最多不超过 40 个字符
	- 每行长度请保持一致
	- 标题的下划线长度请按照一个英文一个字符、一个中文两个字符与标题对齐
	- 其它的修饰符请与英文文档保持一致

此外在译文的头部，您需要插入以下内容::

	.. SPDX-License-Identifier: GPL-2.0
	.. include:: ../disclaimer-zh_CN.rst  #您需要了解该文件的路径，根
					       据您实际翻译的文档灵活调整

	:Original: Documentation/xxx/xxx.rst  #替换为您翻译的英文文档路径

	:翻译:

	 司延腾 Yanteng Si <si.yanteng@linux.dev> #替换为您自己的联系方式

翻译技巧
--------

中文文档有每行 40 字符限制，因为一个中文字符等于 2 个英文字符。但是社区并
没有那么严格，一个诀窍是将您的翻译的内容与英文原文的每行长度对齐即可，这样，
您也不必总是检查有没有超限。

如果您的英文阅读能力有限，可以考虑使用辅助翻译工具，例如 deepseek。但是您
必须仔细地打磨，使译文达到“信达雅”的标准。

**请注意** 社区不接受纯机器翻译的文档，社区工作建立在信任的基础上，请认真对待。

编译和检查
----------

请执行::

	. sphinx_latest/bin/activate
	make cleandocs
	make htmldocs

解决与您翻译的文档相关的 warning 和 error，然后执行::

	make cleandocs	#该步骤不能省略，否则可能不会再次输出真实存在的警告
	make htmldocs
	deactivate

进入 output 目录用浏览器打开您翻译的文档，检查渲染的页面是否正常，如果正常，
继续进行后续步骤，否则请尝试解决。

制作补丁
========

提交改动
--------

执行以下命令，在弹出的交互式页面中填写必要的信息。
::

	git add .
	git commit -s -v

请参考以下信息进行输入::

	docs/zh_CN: Add self-protection index Chinese translation

	Translate .../security/self-protection.rst into Chinese.

	Update the translation through commit b080e52110ea
	("docs: update self-protection __ro_after_init status")
	# 请执行 git log --oneline <您翻译的英文文档路径>，并替换上述内容

	Signed-off-by: Yanteng Si <si.yanteng@linux.dev>
	# 如果您前面的步骤正确执行，该行会自动显示，否则请检查 gitconfig 文件

保存并退出。

**请注意** 以上四行，缺少任何一行，您都将会在第一轮审阅后返工，如果您需要一个
更加明确的示例，请对 zh_CN 目录执行 git log。

导出补丁和制作封面
------------------

这个时候，可以导出补丁，做发送邮件列表最后的准备了。命令行执行::

	git format-patch -N
	# N 要替换为补丁数量，一般 N 大于等于 1

然后命令行会输出类似下面的内容::

	0001-docs-zh_CN-add-xxxxxxxx.patch
	0002-docs-zh_CN-add-xxxxxxxx.patch
	……

测试补丁
--------

内核提供了一个补丁检测脚本，请执行::

	./scripts/checkpatch.pl *.patch

参考脚本输出，解决掉所有的 error 和 warning，通常情况下，只有下面这个
warning 不需要解决::

	WARNING: added, moved or deleted file(s), does MAINTAINERS need updating?

一个简单的解决方法是一次只检查一个补丁，然后打上该补丁，直接对译文进行修改，
然后执行以下命令为补丁追加更改::

	git checkout docs-next
	git checkout -b test-trans-new
	git am 0001-xxxxx.patch
	./scripts/checkpatch.pl 0001-xxxxx.patch
	# 直接修改您的翻译
	git add .
	git am --amend
	# 保存退出
	git am 0002-xxxxx.patch
	……

重新导出再次检测，重复这个过程，直到处理完所有的补丁。

最后，如果检测时没有 warning 和 error 需要被处理或者您只有一个补丁，请跳
过下面这个步骤，否则请重新导出补丁制作封面::

	git format-patch -N --cover-letter --thread=shallow
	# N 要替换为补丁数量，一般 N 大于 1

然后命令行会输出类似下面的内容::

	0000-cover-letter.patch
	0001-docs-zh_CN-add-xxxxxxxx.patch
	0002-docs-zh_CN-add-xxxxxxxx.patch
	……

您需要用编辑器打开 0 号补丁，修改两处内容::

	vim 0000-cover-letter.patch

	...
	Subject: [PATCH 0/N] *** SUBJECT HERE *** #修改该字段，概括您的补丁集都做了哪些事情

	*** BLURB HERE ***			  #修改该字段，详细描述您的补丁集做了哪些事情

	Yanteng Si (1):
	  docs/zh_CN: add xxxxx
	...

如果您只有一个补丁，则可以不制作封面，即 0 号补丁，只需要执行::

	git format-patch -1

把补丁提交到邮件列表
====================

恭喜您，您的文档翻译现在可以提交到邮件列表了。

获取维护者和审阅者邮箱以及邮件列表地址
--------------------------------------

内核提供了一个自动化脚本工具，请执行::

	./scripts/get_maintainer.pl *.patch

将输出的邮箱地址保存下来。

将补丁提交到邮件列表
--------------------

打开上面您保存的邮件地址，执行::

	git send-email *.patch --to <maintainer email addr> --cc <others addr>
	# 一个 to 对应一个地址，一个 cc 对应一个地址，有几个就写几个

执行该命令时，请确保网络通常，邮件发送成功一般会返回 250。

您可以先发送给自己，尝试发出的 patch 是否可以用 'git am' 工具正常打上。
如果检查正常， 您就可以放心的发送到社区评审了。

如果该步骤被中断，您可以检查一下，继续用上条命令发送失败的补丁，一定不要再
次发送已经发送成功的补丁。

积极参与审阅过程并迭代补丁
==========================

补丁提交到邮件列表并不代表万事大吉，您还需要积极回复 maintainer 和
reviewer 的评论，做到每条都有回复，每个回复都落实到位。

如何回复评论
------------

 - 请先将您的邮箱客户端信件回复修改为 **纯文本** 格式，并去除所有签名，尤其是
   企业邮箱。
 - 然后点击回复按钮，并将要回复的邮件带入，
 - 在第一条评论行尾换行，输入您的回复
 - 在第二条评论行尾换行，输入您的回复
 - 直到处理完最后一条评论，换行空两行输入问候语和署名

注意，信件回复请尽量使用英文。

迭代补丁
--------

建议您每回复一条评论，就修改一处翻译。然后重新生成补丁，相信您现在已经具
备了灵活使用 git am --amend 的能力。

每次迭代一个补丁，不要一次多个::

	git am <您要修改的补丁>
	# 直接对文件进行您的修改
	git add .
	git commit --amend

当您将所有的评论落实到位后，导出第二版补丁，并修改封面::

	git format-patch -N -v 2 --cover-letter --thread=shallow

打开 0 号补丁，在 BLURB HERE 处编写相较于上个版本，您做了哪些改动。

然后执行::

	git send-email v2* --to <maintainer email addr> --cc <others addr>

这样，新的一版补丁就又发送到邮件列表等待审阅，之后就是重复这个过程。

审阅周期
--------

因为有时邮件列表比较繁忙，您的邮件可能会被淹没，如果超过两周没有得到任何
回复，请自己回复自己，回复的内容为 Ping.

最终，如果您落实好了所有的评论，并且一段时间后没有最新的评论，您的补丁将
会先进入 Alex 的开发树，然后进入 linux-doc 开发树，最终在下个窗口打开
时合并进 mainline 仓库。

紧急处理
--------

如果您发送到邮件列表之后。发现发错了补丁集，尤其是在多个版本迭代的过程中；
自己发现了一些不妥的翻译；发送错了邮件列表……

git email 默认会抄送给您一份，所以您可以切换为审阅者的角色审查自己的补丁，
并留下评论，描述有何不妥，将在下个版本怎么改，并付诸行动，重新提交，但是
注意频率，每天提交的次数不要超过两次。

新手任务
--------
对于首次参与 Linux 内核中文文档翻译的新手，建议您在 linux 目录中运行以下命令：
::

	./script/checktransupdate.py -l zh_CN``

该命令会列出需要翻译或更新的英文文档，结果同时保存在 checktransupdate.log 中。

关于详细操作说明，请参考：Documentation/translations/zh_CN/doc-guide/checktransupdate.rst。

进阶
----

希望您不只是单纯的翻译内核文档，在熟悉了一起与社区工作之后，您可以审阅其他
开发者的翻译，或者提出具有建设性的主张。与此同时，与文档对应的代码更加有趣，
而且需要完善的地方还有很多，勇敢地去探索，然后提交你的想法吧。

常见的问题
==========

Maintainer 回复补丁不能正常 apply
---------------------------------

这通常是因为您的补丁与邮件列表其他人的补丁产生了冲突，别人的补丁先被 apply 了，
您的补丁集就无法成功 apply 了，这需要您更新本地分支，在本地解决完冲突后再次提交。

请尽量避免冲突，不要多个人同时翻译一个目录。翻译之前可以通过 git log 查看您感
兴趣的目录近期有没有其他人翻译，如果有，请提前私信联系对方，请求其代为发送您
的补丁。如果对方未来一个月内没有提交新补丁的打算，您可以独自发送。

回信被邮件列表拒收
------------------

大部分情况下，是由于您发送了非纯文本格式的信件，请尽量避免使用 webmail，推荐
使用邮件客户端，比如 thunderbird，记得在设置中的回信配置那改为纯文本发送。

如果超过了 24 小时，您依旧没有在<https://lore.kernel.org/linux-doc/>发现您的
邮件，请联系您的网络管理员帮忙解决。
