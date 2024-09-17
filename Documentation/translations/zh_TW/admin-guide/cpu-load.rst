.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Translator: 胡皓文 Hu Haowen <src.res@email.cn>

========
CPU 負載
========

Linux通過``/proc/stat``和``/proc/uptime``導出各種信息，用戶空間工具
如top(1)使用這些信息計算系統花費在某個特定狀態的平均時間。
例如：

    $ iostat
    Linux 2.6.18.3-exp (linmac)     02/20/2007

    avg-cpu:  %user   %nice %system %iowait  %steal   %idle
              10.01    0.00    2.92    5.44    0.00   81.63

    ...

這裡系統認爲在默認採樣周期內有10.01%的時間工作在用戶空間，2.92%的時
間用在系統空間，總體上有81.63%的時間是空閒的。

大多數情況下``/proc/stat``的信息幾乎真實反映了系統信息，然而，由於內
核採集這些數據的方式/時間的特點，有時這些信息根本不可靠。

那麼這些信息是如何被搜集的呢？每當時間中斷觸發時，內核查看此刻運行的
進程類型，並增加與此類型/狀態進程對應的計數器的值。這種方法的問題是
在兩次時間中斷之間系統（進程）能夠在多種狀態之間切換多次，而計數器只
增加最後一種狀態下的計數。

舉例
---

假設系統有一個進程以如下方式周期性地占用cpu::

     兩個時鐘中斷之間的時間線
    |-----------------------|
     ^                     ^
     |_ 開始運行           |
                           |_ 開始睡眠
                           （很快會被喚醒）

在上面的情況下，根據``/proc/stat``的信息（由於當系統處於空閒狀態時，
時間中斷經常會發生）系統的負載將會是0

大家能夠想像內核的這種行爲會發生在許多情況下，這將導致``/proc/stat``
中存在相當古怪的信息::

	/* gcc -o hog smallhog.c */
	#include <time.h>
	#include <limits.h>
	#include <signal.h>
	#include <sys/time.h>
	#define HIST 10

	static volatile sig_atomic_t stop;

	static void sighandler (int signr)
	{
	(void) signr;
	stop = 1;
	}
	static unsigned long hog (unsigned long niters)
	{
	stop = 0;
	while (!stop && --niters);
	return niters;
	}
	int main (void)
	{
	int i;
	struct itimerval it = { .it_interval = { .tv_sec = 0, .tv_usec = 1 },
				.it_value = { .tv_sec = 0, .tv_usec = 1 } };
	sigset_t set;
	unsigned long v[HIST];
	double tmp = 0.0;
	unsigned long n;
	signal (SIGALRM, &sighandler);
	setitimer (ITIMER_REAL, &it, NULL);

	hog (ULONG_MAX);
	for (i = 0; i < HIST; ++i) v[i] = ULONG_MAX - hog (ULONG_MAX);
	for (i = 0; i < HIST; ++i) tmp += v[i];
	tmp /= HIST;
	n = tmp - (tmp / 3.0);

	sigemptyset (&set);
	sigaddset (&set, SIGALRM);

	for (;;) {
		hog (n);
		sigwait (&set, &i);
	}
	return 0;
	}


參考
---

- https://lore.kernel.org/r/loom.20070212T063225-663@post.gmane.org
- Documentation/filesystems/proc.rst (1.8)


謝謝
---

Con Kolivas, Pavel Machek

