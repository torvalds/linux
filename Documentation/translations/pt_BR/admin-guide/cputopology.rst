.. SPDX-License-Identifier: GPL-2.0

================================================================
Como as informações de topologia de CPU são exportadas via sysfs
================================================================

As informações de topologia de CPU são exportadas via sysfs. Os itens
(atributos) são semelhantes à saída de /proc/cpuinfo em algumas arquiteturas.
Eles residem em /sys/devices/system/cpu/cpuX/topology/. Por favor, consulte o
arquivo de ABI: Documentation/ABI/stable/sysfs-devices-system-cpu.

O arquivo independente de arquitetura, drivers/base/topology.c, exporta esses
atributos. No entanto, os arquivos do sysfs relacionados à hierarquia de die,
cluster, book e drawer só serão criados se uma arquitetura fornecer as macros
relacionadas, conforme descrito abaixo.

Para que uma arquitetura ofereça suporte a esse recurso, ela deve definir
algumas dessas macros em include/asm-XXX/topology.h::

	#define topology_physical_package_id(cpu)
	#define topology_die_id(cpu)
	#define topology_cluster_id(cpu)
	#define topology_core_id(cpu)
	#define topology_book_id(cpu)
	#define topology_drawer_id(cpu)
	#define topology_sibling_cpumask(cpu)
	#define topology_core_cpumask(cpu)
	#define topology_cluster_cpumask(cpu)
	#define topology_die_cpumask(cpu)
	#define topology_book_cpumask(cpu)
	#define topology_drawer_cpumask(cpu)

O tipo das ``macros **_id`` é int.
O tipo das ``macros **_cpumask`` é ``(const) struct cpumask *``. Estas últimas
correspondem aos atributos apropriados ``**_siblings`` do sysfs (exceto por
topology_sibling_cpumask(), que corresponde a thread_siblings).

Para manter a consistência em todas as arquiteturas, include/linux/topology.h
fornece definições padrão para qualquer uma das macros acima que não estejam
definidas em include/asm-XXX/topology.h:

1) topology_physical_package_id: -1
2) topology_die_id: -1
3) topology_cluster_id: -1
4) topology_core_id: 0
5) topology_book_id: -1
6) topology_drawer_id: -1
7) topology_sibling_cpumask: apenas a CPU fornecida
8) topology_core_cpumask: apenas a CPU fornecida
9) topology_cluster_cpumask: apenas a CPU fornecida
10) topology_die_cpumask: apenas a CPU fornecida
11) topology_book_cpumask: apenas a CPU fornecida
12) topology_drawer_cpumask: apenas a CPU fornecida

Além disso, as informações de topologia de CPU são fornecidas sob
/sys/devices/system/cpu e incluem estes arquivos. A fonte interna para a saída
está entre colchetes ("[]").

    =========== ==========================================================
    kernel_max: o índice máximo de CPU permitido pela configuração do kernel.
		[NR_CPUS-1]

    offline:	CPUs que não estão online porque foram desligadas via
		HOTPLUG ou excedem o limite de CPUs permitido pela
		configuração do kernel (kernel_max acima).
		[~cpu_online_mask + cpus >= NR_CPUS]

    online:	CPUs que estão online e sendo escalonadas [cpu_online_mask]

    possible:	CPUs para as quais foram alocados recursos e que podem ser
		colocadas online se estiverem presentes. [cpu_possible_mask]

    present:	CPUs que foram identificadas como presentes no
		sistema. [cpu_present_mask]
    =========== ==========================================================

O formato para a saída acima é compatível com cpulist_parse()
[veja <linux/cpumask.h>]. Alguns exemplos a seguir.

Neste exemplo, existem 64 CPUs no sistema, mas as cpus 32-63 excedem
o máximo do kernel, que está limitado a 0..31 devido à opção de
configuração NR_CPUS ser 32. Observe também que as CPUs 2 e 4-31 não estão
online, mas poderiam ser colocadas online, pois estão tanto presentes
quanto possíveis::

     kernel_max: 31
        offline: 2,4-31,32-63
         online: 0-1,3
       possible: 0-31
        present: 0-31

Neste exemplo, a opção de configuração NR_CPUS é 128, mas o kernel foi
iniciado com possible_cpus=144. Existem 4 CPUs no sistema e a cpu2
foi colocada offline manualmente (e é a única CPU que pode ser colocada
online.)::

     kernel_max: 127
        offline: 2,4-127,128-143
         online: 0-1,3
       possible: 0-127
        present: 0-3

Consulte Documentation/core-api/cpu_hotplug.rst para o parâmetro de inicialização
do kernel possible_cpus=NUM, bem como para obter mais informações sobre as
várias cpumasks.
