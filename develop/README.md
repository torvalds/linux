# カーネルビルド

筆者がカーネルを動かした環境は以下の通り.

OS は ubuntu22.04 を用いています.
PC メモリは 32G

```
$uname -a
Linux naoto-MB-K700 6.8.0-40-generic #40~22.04.3-Ubuntu SMP PREEMPT_DYNAMIC Tue Jul 30 17:30:19 UTC 2 x86_64 x86_64 x86_64 GNU/Linux
naoto@naoto-MB-K700:/boot $
```

# 手順

## 準備

カーネルビルドに必要なパッケージのインストール

```
$sudo apt install git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison
```

カーネルのバージョン(執筆当時 2024 年 9 月 22 日)`6.11` のソースコードのインストール

git clone の方が良いかも

```
$ wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.11.tar.xz
$tar xvf linux-6.11.tar.xz
$cd linux-6.11
```

ものすごく時間がかかる

```
$git clone https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

$git checkout v6.11
```

浅いクローンだと良いかも

```
$git clone --depth 1 --branch v6.11 https://github.com/torvalds/linux.git

$cd linux
$ git checkout -b test-run

```

`git clone --depth 1 --branch v6.8 https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
cd linux` 現在のカーネルのバージョンに合わせたものを利用した方が良い
`uname -a`

build する際に config ファイルの設定をする.
今回は`defconfig`だが、
必要ならば`make menuconfig`で設定を行う

```
$make defconfig
```

## ビルド

### make

`make`コマンドを用いてビルドする.`-j`オプションをつけることで並列数を調節できる

```
$ make -j ${nproc}
```

筆者の環境では 4 コアで build したので 例 `$make -j 4`となる.

なお、ビルドエラーメッセージを出力させたい場合を考慮して以下のようにすると良いかも

```
$make -j$(nproc) > build.log 2>&1
```

### 初期 RAM ディスクの作成

#### backup

```
sudo cp /boot/grub/grub.cfg /boot/grub/grub.cfg.backup
```

#### backup 終わり

```
sudo apt-get install linux-headers-$(uname -r)
```

新しくビルドしたカーネル用の初期 RAM ディスクを作成する. 以下のコマンドで作成する：
ここで`6.11`はカーネルのバージョンなので、バージョンが変われば適宜変更する

```
$ sudo mkinitramfs -o /boot/initrd.img-6.11 6.11
```

これが無理

打ち込んだコマンド
`sudo apt install --reinstall initramfs-tools`
もしかしたら先に`make modules_install`しないとダメかも

GRUB ブートローダーを更新して、新しいカーネルを認識させます。
`update-grub` コマンドは通常、既存の GRUB 設定を上書きするのではなく、新しいカーネルエントリを追加します。既存のカーネルオプションは保持されます。

```
$ sudo update-grub
```

```
$ sudo make modules_install
$ sudo make install
```

この時点で`/boot`下に作られる
下はその結果.
9.22 18:46(9.22 18:47?) に作成されたものが該当する

```
aoto@naoto-MB-K700:/boot $ ls -l
合計 267056
-rw-r--r-- 1 root root  7603305  9月 22 18:47 System.map-6.11.0
-rw------- 1 root root  8269177  6月 18 22:18 System.map-6.5.0-44-generic
-rw-r--r-- 1 root root   445868  9月 22 16:26 System.map-6.6.32
-rw-r--r-- 1 root root   445868  9月 22 16:26 System.map-6.6.32.old
-rw------- 1 root root  8654773  7月 30 23:33 System.map-6.8.0-40-generic
-rw-r--r-- 1 root root   142194  9月 22 18:47 config-6.11.0
-rw-r--r-- 1 root root   280697  6月 18 22:18 config-6.5.0-44-generic
-rw-r--r-- 1 root root    32628  9月 22 16:26 config-6.6.32
-rw-r--r-- 1 root root    32628  9月 22 16:26 config-6.6.32.old
-rw-r--r-- 1 root root   287007  7月 30 23:33 config-6.8.0-40-generic
drwx------ 3 root root     4096  1月  1  1970 efi
lrwxrwxrwx 1 root root       17  9月 22 18:47 initrd.img -> initrd.img-6.11.0
-rw-r--r-- 1 root root 17547094  9月 22 18:46 initrd.img-6.11
-rw-r--r-- 1 root root 22300012  9月 22 18:47 initrd.img-6.11.0
-rw-r--r-- 1 root root 70997206  7月 24 21:27 initrd.img-6.5.0-44-generic
-rw-r--r-- 1 root root 17547284  9月 22 16:26 initrd.img-6.6.32
-rw-r--r-- 1 root root 72751664  9月  1 20:09 initrd.img-6.8.0-40-generic
lrwxrwxrwx 1 root root       17  9月 22 18:47 initrd.img.old -> initrd.img-6.6.32
-rw-r--r-- 1 root root   182800  2月  7  2022 memtest86+.bin
-rw-r--r-- 1 root root   184476  2月  7  2022 memtest86+.elf
-rw-r--r-- 1 root root   184980  2月  7  2022 memtest86+_multiboot.bin
lrwxrwxrwx 1 root root       14  9月 22 18:47 vmlinuz -> vmlinuz-6.11.0
-rw-r--r-- 1 root root 13325312  9月 22 18:47 vmlinuz-6.11.0
-rw------- 1 root root 14263016  6月 18 22:19 vmlinuz-6.5.0-44-generic
-rw-r--r-- 1 root root  1503744  9月 22 16:26 vmlinuz-6.6.32
-rw-r--r-- 1 root root  1503744  9月 22 16:26 vmlinuz-6.6.32.old
-rw------- 1 root root 14928264  7月 31 00:17 vmlinuz-6.8.0-40-generic
lrwxrwxrwx 1 root root       14  9月 22 16:26 vmlinuz.old -> vmlinuz-6.6.32
naoto@naoto-MB-K700:/boot $
```

<!--
重複していたので...
続いて ram ディスクを作成する

```
$ sudo mkinitramfs -o /boot/initrd.img-6.11.0 6.11.0
```

grub のアップデート

```
$sudo update-grub
```

途中結果

```
naoto@naoto-MB-K700:~/kernel_build/linux-6.11 $ sudo mkinitramfs -o /boot/initrd.img-6.11.0 6.11.0
W: Possible missing firmware /lib/firmware/i915/bmg_dmc.bin for built-in driver i915
W: Possible missing firmware /lib/firmware/i915/xe2lpd_dmc.bin for built-in driver i915
W: Possible missing firmware /lib/firmware/rtl_nic/rtl8126a-2.fw for built-in driver r8169
naoto@naoto-MB-K700:~/kernel_build/linux-6.11 $ sudo update-grub
Sourcing file `/etc/default/grub'
Sourcing file `/etc/default/grub.d/init-select.cfg'
/usr/sbin/grub-mkconfi
```
-->

# QEMU で動かす

QEMU インストール. 以下のコマンドを実行

```
$sudo apt install qemu-system qemu-utils
```

# memo

テスト用の最小ルートファイルシステムを用意する必要があります。例えば、BusyBox を使用した最小のルートファイルシステムを作成できます。ただし、これは複雑なプロセスなので、ここでは既存のディストリビューションのイメージを使用する方法を説明します。

ubuntu 22.04 ->24 に変更する

```
$wget https://cloud-images.ubuntu.com/minimal/releases/jammy/release/ubuntu-22.04-minimal-cloudimg-amd64.img
```

image は小さすぎるので resize させる

```
$qemu-img resize ubuntu-22.04-minimal-cloudimg-amd64.img +5G
```

## 動かす

`user-data`というファイルを作り、以下を記述する。
ここの`mypassword`は後にログインで必要なパスワードとなる

```
#cloud-config
password: mypassword
chpasswd: { expire: False }
ssh_pwauth: True
```

この`user-data`ファイルを用いてディスクイメージを作成

```
$sudo apt install cloud-image-utils
```

```
$ cloud-localds user-data.img user-data
```

qemu を起動する

```
qemu-system-x86_64 \
  -kernel arch/x86/boot/bzImage \
  -initrd /boot/initrd.img-6.11 \
  -append "console=ttyS0 root=/dev/sda1 rw" \
  -nographic \
  -m 1024M \
  -drive file=ubuntu-22.04-minimal-cloudimg-amd64.img,format=qcow2 \
  -drive file=user-data.img,format=raw
```

コマンドの意味
`-m 1024M`: 1GB のメモリで qemu を走らせる

起動する[Image](run_kernel.png)
!()
ので`username`を`ubuntu`, `password`は`mypassword`となる(`user-data`ファイルに記述した内容)

# 注意

## エラー 1

```
mount: mounting /dev/sda on /root failed: Invalid argument
```

が出た場合、これはマウントに失敗している
`/dev/sda`は実行環境によって変わるので

qemu の`append`オプションの`root=/dev/sda` をたとえば
`root=/dev/sda1` に変更させるなど

## エラー 2

```
[  119.422681] Out of memory: Killed process 488 (cloud-init) total-vm:42660kB, anon-rss:26076kB, file-rss:52kB, 0
```

メモリ不足に陥り`cloud init process`が強制終了.
QEMU で `-m 1024M`の部分のメモリ設定を大きくする

# メモ

テスト用のディスクイメージと 今回ビルドして作られたバイナリはどういう関係か?

カーネルイメージ（ビルドされたバイナリ）:

通常 arch/x86/boot/bzImage として作成されます。
これは Linux カーネル本体で、OS の中核となる部分です。
メモリ管理、プロセス管理、デバイスドライバなどの基本的な機能を提供します。
テスト用のディスクイメージ（Ubuntu クラウドイメージ）:

通常 ubuntu-22.04-minimal-cloudimg-amd64.img のような名前です。
これはルートファイルシステムを含む完全な OS イメージです。
ユーザーランドプログラム、設定ファイル、ライブラリなどが含まれています。
関係:

補完関係:

カーネルイメージは OS の中核ですが、それだけでは完全なシステムを構成できません。
ディスクイメージはユーザーランド環境を提供しますが、それ自体にはカーネルが含まれていません。
両者を組み合わせることで、完全に動作するシステムが構成されます。
起動プロセス:

QEMU は最初にカーネルイメージ（bzImage）をロードし、実行します。
カーネルは起動時にディスクイメージ内のルートファイルシステムをマウントします。
その後、ディスクイメージ内の init プロセスが開始され、システムの残りの部分が初期化されます。
テストの目的:

新しくビルドしたカーネルの機能や安定性をテストするために、既知の安定したディスクイメージ（Ubuntu クラウドイメージ）と組み合わせます。
これにより、カーネルの変更がシステム全体にどのような影響を与えるかを確認できます。
独立性:

カーネルイメージは、異なるディストリビューションや設定と組み合わせて使用できます。
ディスクイメージは、異なるバージョンや設定のカーネルと組み合わせて使用できます。
開発とテストのワークフロー:

カーネル開発者は、カーネルコードを変更し、新しいカーネルイメージをビルドします。
その新しいカーネルイメージを、変更されていない標準的なディスクイメージと組み合わせてテストします。
これにより、カーネルの変更が原因の問題と、ディスクイメージ（ユーザーランド）の問題を区別しやすくなります。
この関係を理解することで、カーネル開発とテストのプロセスがより明確になり、問題が発生した際のデバッグも容易になります。
