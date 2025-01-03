.. SPDX-License-Identifier: GPL-2.0

术语表
======

这不是一个完善的术语表，我们只是将有争议的和陌生的翻译词汇记录于此，
它的篇幅应该根据内核文档翻译的需求而增加。新词条最好随翻译补丁一起
提交，且仅在以下情况下收录新词条：

        - 在翻译过程中遇到陌生词汇，且尚无翻译先例的；
        - 在审阅过程中，针对某词条出现了不同的翻译意见；
        - 使用频率不高的词条和首字母缩写类型的词条；
        - 已经存在且有歧义的词条翻译。


* atomic: 原子的，一般指不可中断的极小的临界区操作。
* DVFS: 动态电压频率升降。（Dynamic Voltage and Frequency Scaling）
* EAS: 能耗感知调度。（Energy Aware Scheduling）
* flush: 刷新，一般指对cache的冲洗操作。
* fork: 创建, 通常指父进程创建子进程。
* futex: 快速用户互斥锁。（fast user mutex）
* guest halt polling: 客户机停机轮询机制。
* HugePage: 巨页。
* hypervisor: 虚拟机超级管理器。
* memory barriers: 内存屏障。
* MIPS: 每秒百万指令。（Millions of Instructions Per Second）,注意与mips指令集区分开。
* mutex: 互斥锁。
* NUMA: 非统一内存访问。
* OpenCAPI: 开放相干加速器处理器接口。（Open Coherent Accelerator Processor Interface）
* OPP: 操作性能值。
* overhead: 开销，一般指需要消耗的计算机资源。
* PELT: 实体负载跟踪。（Per-Entity Load Tracking）
* sched domain: 调度域。
* semaphores: 信号量。
* spinlock: 自旋锁。
* watermark: 水位，一般指页表的消耗水平。
* PTE: 页表项。（Page Table Entry）
