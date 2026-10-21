.. SPDX-License-Identifier: GPL-2.0

=======================================
Adicionando uma Nova Chamada de Sistema
=======================================

Este documento descreve o que está envolvido na adição de uma nova chamada de
sistema (system call) ao kernel Linux, indo além dos conselhos normais de
submissão em
:ref:`Documentation/process/submitting-patches.rst <submittingpatches>`.


Alternativas às Chamadas de Sistema
-----------------------------------

A primeira coisa a se considerar ao adicionar uma nova chamada de sistema é se
uma das alternativas poderia ser mais adequada. Embora as chamadas de sistema
sejam os pontos de interação mais tradicionais e óbvios entre o espaço do
usuário (userspace) e o kernel, existem outras possibilidades -- escolha o que
melhor se adapta à sua interface.

 - Se as operações envolvidas puderem ser moldadas para se parecerem com um
   objeto do tipo arquivo, pode fazer mais sentido criar um novo sistema de
   arquivos ou dispositivo. Isso também torna mais fácil encapsular a nova
   funcionalidade em um módulo de kernel, em vez de exigir que ela seja
   incorporada ao kernel principal.

     - Se a nova funcionalidade envolver operações em que o kernel notifica o
       espaço do usuário de que algo aconteceu, retornar um novo descritor de
       arquivo (file descriptor) para o objeto relevante permite que o espaço
       do usuário use ``poll``/``select``/``epoll`` para receber essa
       notificação.
     - No entanto, as operações que não se mapeiam para operações do tipo
       :manpage:`read(2)`/:manpage:`write(2)` precisam ser implementadas como
       requisições :manpage:`ioctl(2)`, o que pode levar a uma API um tanto
       quanto opaca.

 - Se você estiver apenas expondo informações do sistema em tempo de execução,
   um novo nó no sysfs (veja ``Documentation/filesystems/sysfs.rst``) ou no
   sistema de arquivos ``/proc`` pode ser mais apropriado. No entanto, o acesso
   a esses mecanismos exige que o sistema de arquivos relevante esteja montado,
   o que pode não ser sempre o caso (por exemplo, em um ambiente com namespaces,
   sandboxed ou chrooted). Evite adicionar qualquer API ao debugfs, pois este
   não é considerado uma interface de "produção" para o espaço do usuário.
 - Se a operação for específica para um arquivo ou descritor de arquivo de um
   determinado objeto, então uma opção de comando adicional para :manpage:`fcntl(2)`
   pode ser mais adequada. Contudo, o :manpage:`fcntl(2)` é uma chamada de sistema
   de multiplexação que oculta muita complexidade, portanto, esta opção é melhor
   para quando a nova função for intimamente análoga à funcionalidade existente
   do :manpage:`fcntl(2)`, ou se a nova funcionalidade for muito simples (por
   exemplo, obter/definir uma flag simples relacionada a um descritor de arquivo).
 - Se a operação for específica para uma tarefa (task) ou processo específico,
   então uma opção de comando adicional para :manpage:`prctl(2)` pode ser mais
   apropriada. Assim como no caso do :manpage:`fcntl(2)`, esta chamada de sistema
   é um multiplexador complicado, sendo melhor reservá-la para análogos próximos
   de comandos ``prctl()`` existentes ou para obter/definir uma flag simples
   relacionada a um processo.


Projetando a API: Planejando a Extensibilidade
----------------------------------------------

Uma nova chamada de sistema faz parte da API do kernel e deve ser suportada
indefinidamente. Sendo assim, é uma excelente ideia discutir explicitamente a
interface na lista de discussão do kernel (LKML), e é crucial planejar extensões
futuras para essa interface.

(A tabela de chamadas de sistema está repleta de exemplos históricos onde isso
não foi feito, juntamente com as respectivas chamadas de sistema de acompanhamento
-- ``eventfd``/``eventfd2``, ``dup2``/``dup3``, ``inotify_init``/``inotify_init1``,
``pipe``/``pipe2``, ``renameat``/``renameat2`` -- portanto, aprenda com a história
do kernel e planeje as extensões desde o início.)

Para chamadas de sistema mais simples que recebem apenas alguns argumentos, a
maneira preferencial de permitir extensibilidade futura é incluir um argumento de
flags na chamada de sistema. Para garantir que os programas do espaço do usuário
possam usar flags de forma segura entre diferentes versões do kernel, verifique
se o valor de flags contém qualquer flag desconhecida e rejeite a chamada de
sistema (com ``EINVAL``) se contiver::

    if (flags & ~(THING_FLAG1 | THING_FLAG2 | THING_FLAG3))
        return -EINVAL;

(Se nenhum valor de flag for utilizado ainda, verifique se o argumento de flags
é zero.)

Para chamadas de sistema mais sofisticadas que envolvem um número maior de
argumentos, prefere-se encapsular a maioria dos argumentos em uma estrutura
(struct) que é passada por meio de um ponteiro. Esse tipo de estrutura pode
lidar com extensões futuras incluindo um argumento de tamanho (size) na própria
estrutura::

    struct xyzzy_params {
        u32 size; /* o espaço do usuário define p->size = sizeof(struct xyzzy_params) */
        u32 param_1;
        u64 param_2;
        u64 param_3;
    };

Desde que qualquer campo adicionado subsequentemente, digamos ``param_4``, seja
projetado de forma que um valor zero mantenha o comportamento anterior, isso
permitirá lidar com a divergência de versões em ambas as direções:

 - Para lidar com um programa de espaço do usuário mais novo chamando um kernel
   mais antigo, o código do kernel deve verificar se qualquer memória além do
   tamanho da estrutura que ele espera está zerada (efetivamente verificando
   se ``param_4 == 0``).
 - Para lidar com um programa de espaço do usuário mais antigo chamando um kernel
   mais novo, o código do kernel pode preencher com zero (zero-extend) a
   instância menor da estrutura (efetivamente definindo ``param_4 = 0``).

Veja :manpage:`perf_event_open(2)` e a função ``perf_copy_attr()`` (em
``kernel/events/core.c``) para um exemplo desta abordagem.


Projetando a API: Outras Considerações
--------------------------------------

Se a sua nova chamada de sistema permitir que o espaço do usuário se refira a
um objeto do kernel, ela deve usar um descritor de arquivo (file descriptor)
como o handle (identificador) para esse objeto -- não invente um novo tipo de
handle de objeto para o espaço do usuário quando o kernel já possui mecanismos
e semânticas bem definidas para o uso de descritores de arquivo.

Se a sua nova chamada de sistema (2) de fato retornar un novo descritor de
arquivo, então o argumento de flags deve incluir um valor que seja equivalente
a definir ``O_CLOEXEC`` no novo FD. Isso torna possível para o espaço do usuário
fechar a janela de tempo entre a chamada ``()`` e a execução de
``fcntl(fd, F_SETFD, FD_CLOEXEC)``, onde um ``fork()`` e ``execve()`` inesperados
em outra thread poderiam vazar um descritor para o programa executado. (Contudo,
resista à tentação de reutilizar o valor real da constante ``O_CLOEXEC``, pois
ela é específica de cada arquitetura e faz parte de um espaço de numeração de
flags ``O_*`` que está bastante cheio.)

Se a sua chamada de sistema retornar um novo descritor de arquivo, você também
deve considerar o que significa usar a família de chamadas de sistema
:manpage:`poll(2)` nesse descritor de arquivo. Tornar um descritor de arquivo
pronto para leitura ou escrita é a maneira normal de o kernel indicar ao espaço
do usuário que um evento ocorreu no objeto correspondente do kernel.

Se a sua nova chamada de sistema (2) envolver um argumento de nome de arquivo
(filename)::

    int sys_xyzzy(const char __user *path, ..., unsigned int flags);

você também deve considerar se uma versão xyzzyat(2) seria mais apropriada::

    int sys_xyzzyat(int dfd, const char __user *path, ..., unsigned int flags);

Isso permite maior flexibilidade para a forma como o espaço do usuário especifica
o arquivo em questão; em particular, permite que o espaço do usuário solicite a
funcionalidade para um descritor de arquivo já aberto usando a flag
``AT_EMPTY_PATH``, fornecendo efetivamente uma operação fxyzzy(3) de graça::

 - xyzzyat(AT_FDCWD, path, ..., 0) é equivalente a (path,...)
 - xyzzyat(fd, "", ..., AT_EMPTY_PATH) é equivalente a fxyzzy(fd, ...)

(Para mais detalhes sobre a justificativa das chamadas \*at(), veja a página de
manual :manpage:`openat(2)`; para um exemplo de AT_EMPTY_PATH, veja a página de
manual :manpage:`fstatat(2)`.)

Se a sua nova chamada de sistema (2) envolver um parâmetro que descreve um
deslocamento (offset) dentro de um arquivo, mude o seu tipo para ``loff_t`` para
que offsets de 64 bits possam ser suportados mesmo em arquiteturas de 32 bits.

Se a sua nova chamada de sistema (2) envolver funcionalidades privilegiadas,
ela precisa ser governada pelo bit de capacidade (capability) do Linux apropriado
(verificado com uma chamada a ``capable()``), conforme descrito na página de
manual :manpage:`capabilities(7)`. Escolha um bit de capacidade existente que governe
funcionalidades relacionadas, mas tente evitar combinar muitas funções que tenham
apenas uma vaga relação sob o mesmo bit, pois isso vai contra o propósito das
capabilities de dividir o poder do root. Em particular, evite adicionar novos
usos para a capacidade ``CAP_SYS_ADMIN``, que já é excessivamente generalista.

Se a sua nova chamada de sistema (2) manipular um processo diferente do
processo que a chamou, ela deve ser restrita (usando uma chamada a
``ptrace_may_access()``) para que apenas um processo chamador com as mesmas
permissões do processo alvo, ou com as capacidades necessárias, possa manipular
o processo alvo.

Finalmente, esteja ciente de que algumas arquiteturas não-x86 lidam melhor se os
parâmetros da chamada de sistema que são explicitamente de 64 bits caírem em
argumentos de numeração ímpar (ou seja, parâmetro 1, 3, 5), para permitir o uso
de pares contíguos de registradores de 32 bits. (Esta preocupação não se aplica
se os argumentos fizerem parte de uma estrutura que é passada por meio de um
ponteiro.)


Propondo a API
--------------

Para tornar as novas chamadas de sistema fáceis de revisar, é melhor dividir o
conjunto de patches (patchset) em blocos separados. Estes devem incluir, pelo
menos, os seguintes itens como commits distintos (cada um dos quais é descrito
mais adiante):

 - A implementação central da chamada de sistema, juntamente com protótipos,
   numeração genérica, alterações no Kconfig e a implementação de stub de realinhamento (fallback stub).
 - A fiação (wiring up) da nova chamada de sistema para uma arquitetura em
   particular, geralmente x86 (incluindo todas as variantes x86_64, x86_32 e x32).
 - Uma demonstração do uso da nova chamada de sistema no espaço do usuário por
   meio de um selftest em ``tools/testing/selftests/``.
 - Um rascunho da página de manual (man-page) para a nova chamada de sistema,
   seja como texto simples na carta de apresentação (cover letter) ou como um
   patch para o repositório (separado) de man-pages.

Novas propostas de chamadas de sistema, como qualquer alteração na API do
kernel, devem sempre ser enviadas com cópia (cc'ed) para linux-api@vger.kernel.org.


Implementação Genérica de Chamadas de Sistema
---------------------------------------------

O ponto de entrada principal para a sua nova chamada de sistema (2) será chamado
de ``sys_xyzzy()``, mas você deve adicionar esse ponto de entrada com a macro
``SYSCALL_DEFINEn()`` apropriada, em vez de fazer isso explicitamente. O 'n'
indica o número de argumentos da chamada de sistema, e a macro recebe o nome da
chamada de sistema seguido pelos pares (tipo, nome) para os parâmetros como
argumentos. O uso dessa macro permite que os metadados sobre a nova chamada de
sistema fiquem disponíveis para outras ferramentas.

O novo ponto de entrada também precisa de um protótipo de função correspondente
em ``include/linux/syscalls.h``, marcado como asmlinkage para corresponder à
maneira como as chamadas de sistema são invocadas::

    asmlinkage long sys_xyzzy(...);

Algumas arquiteturas (por exemplo, x86) possuem suas próprias tabelas de syscall
específicas da arquitetura, mas várias outras arquiteturas compartilham uma tabela
de syscall genérica. Adicione a sua nova chamada de sistema à lista genérica
adicionando uma entrada na lista em ``include/uapi/asm-generic/unistd.h``::

    #define __NR_xyzzy 292
    __SYSCALL(__NR_xyzzy, sys_xyzzy)

Atualize também a contagem de __NR_syscalls para refletir a chamada de sistema
adicional, e observe que se múltiplas novas chamadas de sistema forem adicionadas
na mesma janela de mesclagem (merge window), o número da sua nova syscall poderá
ser ajustado para resolver conflitos.

O arquivo ``kernel/sys_ni.c`` fornece uma implementação de stub de fallback para
cada chamada de sistema, retornando ``-ENOSYS``. Adicione a sua nova chamada de
sistema aqui também::

    COND_SYSCALL(sys_xyzzy);

A sua nova funcionalidade de kernel, e a chamada de sistema que a controla, deve
normalmente ser opcional, portanto adicione uma opção ``CONFIG`` (tipicamente em
``init/Kconfig``) para ela. Como de costume para novas opções ``CONFIG``:

 - Inclua uma descrição da nova funcionalidade e da chamada de sistema controlada
   pela opção.
 - Faça a opção depender de EXPERT se ela deve ser ocultada dos usuários normais.
 - Faça com que quaisquer novos arquivos de código-fonte que implementem a função
   sejam dependentes da opção CONFIG no Makefile (por exemplo,
   ``obj-$(CONFIG_XYZZY_SYSCALL) += xyzzy.o``).
 - Verifique duas vezes se o kernel ainda compila com a nova opção CONFIG desativada.

Para resumir, você precisa de um commit que inclua:

 - Opção ``CONFIG`` para a nova função, normalmente em ``init/Kconfig``
 - ``SYSCALL_DEFINEn(, ...)`` para o ponto de entrada
 - Protótipo correspondente em ``include/linux/syscalls.h``
 - Entrada na tabela genérica em ``include/uapi/asm-generic/unistd.h``
 - Stub de fallback em ``kernel/sys_ni.c``


.. _pt_BR_syscall_generic_6_11:

Desde a versão 6.11
~~~~~~~~~~~~~~~~~~~

A partir da versão 6.11 do kernel, a implementação de chamadas de sistema
genéricas para as seguintes arquiteturas não requer mais modificações em
``include/uapi/asm-generic/unistd.h``:

 - arc
 - arm64
 - csky
 - hexagon
 - loongarch
 - nios2
 - openrisc
 - riscv

Em vez disso, você precisa atualizar ``scripts/syscall.tbl`` e, se aplicável,
ajustar ``arch/*/kernel/Makefile.syscalls``.

Como o ``scripts/syscall.tbl`` serve como uma tabela de syscall comum para
múltiplas arquiteturas, uma nova entrada é necessária nesta tabela::

    468   common        sys_xyzzy

Note que adicionar uma entrada ao ``scripts/syscall.tbl`` com a ABI "common"
também afeta todas as arquiteturas que compartilham essa tabela. Para alterações
mais limitadas ou específicas de uma arquitetura, considere usar uma ABI
específica da arquitetura ou definir uma nova.

Se uma nova ABI, digamos ``xyz``, for introduzida, as atualizações
correspondentes também devem ser feitas em ``arch/*/kernel/Makefile.syscalls``::

    syscall_abis_{32,64} += xyz (...)

Para resumir, você precisa de um commit que inclua:

 - Opção ``CONFIG`` para a nova função, normalmente em ``init/Kconfig``
 - ``SYSCALL_DEFINEn(, ...)`` para o ponto de entrada
 - Protótipo correspondente em ``include/linux/syscalls.h``
 - Nova entrada em ``scripts/syscall.tbl``
 - (Se necessário) Atualizações de Makefile em ``arch/*/kernel/Makefile.syscalls``
 - Stub de fallback em ``kernel/sys_ni.c``


Implementação de Chamadas de Sistema em x86
-------------------------------------------

Para interligar (wire up) a sua nova chamada de sistema nas plataformas x86, você
precisa atualizar as tabelas mestras de syscall. Assumindo que a sua nova chamada
de sistema não seja especial de alguma forma (veja abaixo), isso envolve uma
entrada "common" (para x86_64 e x32) em
``arch/x86/entry/syscalls/syscall_64.tbl``::

    333   common        sys_xyzzy

e uma entrada "i386" em ``arch/x86/entry/syscalls/syscall_32.tbl``::

    380   i386          sys_xyzzy

Novamente, esses números estão sujeitos a alterações caso ocorram conflitos na
janela de mesclagem (merge window) relevante.

Chamadas de Sistema de Compatibilidade (Genéricas)
--------------------------------------------------

Para a maioria das chamadas de sistema, a mesma implementação de 64 bits pode
ser invocada mesmo quando o programa do espaço do usuário é, ele próprio, de 32
bits; mesmo se os parâmetros da chamada de sistema incluírem um ponteiro
explícito, isso é tratado de forma transparente.

No entanto, existem algumas situações em que uma camada de compatibilidade
(compatibility layer) é necessária para lidar com as diferenças de tamanho entre
32 bits e 64 bits.

A primeira é se o kernel de 64 bits também suportar programas de espaço do
usuário de 32 bits e, portanto, precisar analisar áreas de memória
(``__user``) que poderiam conter valores de 32 bits ou 64 bits. Em particular,
isso é necessário sempre que um argumento de chamada de sistema for:

 - um ponteiro para um ponteiro
 - um ponteiro para uma struct que contém um ponteiro (por exemplo,
   ``struct iovec __user *``)
 - um ponteiro para um tipo integral de tamanho variável (``time_t``,
   ``off_t``, ``long``, ...)
 - um ponteiro para uma struct que contém um tipo integral de tamanho variável.

A segunda situação que requer uma camada de compatibilidade é se um dos
argumentos da chamada de sistema tiver um tipo que é explicitamente de 64 bits,
mesmo em uma arquitetura de 32 bits, por exemplo, ``loff_t`` ou ``__u64``. Neste
caso, um valor que chega ao kernel de 64 bits vindo de uma aplicação de 32 bits
será dividido em dois valores de 32 bits, que precisarão ser remontados na
camada de compatibilidade.

(Note que um argumento de chamada de sistema que seja um ponteiro para um tipo
explícito de 64 bits **não** precisa de uma camada de compatibilidade; por
exemplo, os argumentos do :manpage:`splice(2)` do tipo ``loff_t __user *`` não
disparam a necessidade de uma chamada de sistema ``compat_``.)

A versão de compatibilidade da chamada de sistema é chamada de
``compat_sys_xyzzy()`` e é adicionada com a macro ``COMPAT_SYSCALL_DEFINEn()``,
de forma análoga à macro SYSCALL_DEFINEn. Esta versão da implementação roda como
parte de um kernel de 64 bits, mas espera receber valores de parâmetros de 32
bits e faz o que for necessário para lidar com eles. (Tipicamente, a versão
``compat_sys_`` converte os valores para versões de 64 bits e chama a versão
``sys_``, ou ambas chamam uma função interna comum de implementação).

O ponto de entrada compat também precisa de um protótipo de função
correspondente em ``include/linux/compat.h``, marcado como asmlinkage para
corresponder à maneira como as chamadas de sistema são invocadas::

    asmlinkage long compat_sys_xyzzy(...);

Se a chamada de sistema envolver uma estrutura cujo layout seja diferente em
sistemas de 32 bits e 64 bits, digamos ``struct xyzzy_args``, então o arquivo de
cabeçalho ``include/linux/compat.h`` também deve incluir uma versão compat da
estrutura (``struct compat_xyzzy_args``), onde cada campo de tamanho variável
tenha o tipo ``compat_`` correspondente ao tipo na ``struct xyzzy_args``. A
rotina ``compat_sys_xyzzy()`` pode então usar essa estrutura ``compat_`` para
analisar os argumentos vindos de uma invocação de 32 bits.

Por exemplo, se existirem os campos::

    struct xyzzy_args {
        const char __user *ptr;
        __kernel_long_t varying_val;
        u64 fixed_val;
        /* ... */
    };

na struct xyzzy_args, então a struct compat_xyzzy_args teria::

    struct compat_xyzzy_args {
        compat_uptr_t ptr;
        compat_long_t varying_val;
        u64 fixed_val;
        /* ... */
    };

A lista genérica de chamadas de sistema também precisa de ajustes para permitir
a versão compat; a entrada em ``include/uapi/asm-generic/unistd.h`` deve usar
``__SC_COMP`` em vez de ``__SYSCALL``::

    #define __NR_xyzzy 292
    __SC_COMP(__NR_xyzzy, sys_xyzzy, compat_sys_xyzzy)

Para resumir, você precisa de:

 - uma macro ``COMPAT_SYSCALL_DEFINEn(, ...)`` para o ponto de entrada compat
 - protótipo correspondente em ``include/linux/compat.h``
 - (se necessário) struct de mapeamento de 32 bits em ``include/linux/compat.h``
 - instância de ``__SC_COMP``, e não de ``__SYSCALL``, em
   ``include/uapi/asm-generic/unistd.h``

Desde a versão 6.11
~~~~~~~~~~~~~~~~~~~

Isso se aplica a todas as arquiteturas listadas em
:ref:`Desde a versão 6.11<pt_BR_syscall_generic_6_11>` sob "Implementação Genérica de
Chamadas de Sistema", exceto arm64. Veja
:ref:`Chamadas de Sistema de Compatibilidade (arm64)<pt_BR_compat_arm64>` para mais
informações.

Você precisa estender a entrada em ``scripts/syscall.tbl`` com uma coluna extra
para indicar que um programa de espaço do usuário de 32 bits rodando em um
kernel de 64 bits deve atingir o ponto de entrada compat::

    468   common          sys_xyzzy    compat_sys_xyzzy

Para resumir, você precisa de:

 - ``COMPAT_SYSCALL_DEFINEn(, ...)`` para o ponto de entrada compat
 - Protótipo correspondente em ``include/linux/compat.h``
 - Modificação da entrada em ``scripts/syscall.tbl`` para incluir uma coluna
   "compat" extra
 - (Se necessário) Struct de mapeamento de 32 bits em ``include/linux/compat.h``


.. _pt_BR_compat_arm64:

Chamadas de Sistema de Compatibilidade (arm64)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

No arm64, existe uma tabela de syscall dedicada para chamadas de sistema de
compatibilidade voltadas para o espaço do usuário de 32 bits (AArch32):
``arch/arm64/tools/syscall_32.tbl``. Você precisa adicionar uma linha adicional
a esta tabela especificando o ponto de entrada compat::

    468   common          sys_xyzzy    compat_sys_xyzzy


Chamadas de Sistema de Compatibilidade (x86)
--------------------------------------------

Para interligar a arquitetura x86 de uma chamada de sistema com uma versão de
compatibilidade, as entradas nas tabelas de syscall precisam ser ajustadas.

Primeiro, a entrada em ``arch/x86/entry/syscalls/syscall_32.tbl`` ganha uma
coluna extra para indicar que um programa de espaço do usuário de 32 bits rodando
em um kernel de 64 bits deve atingir o ponto de entrada compat::

    380   i386          sys_xyzzy    __ia32_compat_sys_xyzzy

Segundo, você precisa definir o que deve acontecer para a versão da ABI x32 da
nova chamada de sistema. Há uma escolha aqui: o layout dos argumentos deve
corresponder à versão de 64 bits ou à versão de 32 bits.

Se houver um ponteiro para um ponteiro envolvido, a decisão é fácil: x32 é
ILP32 (inteiro, long e ponteiro possuem 32 bits), portanto o layout deve
corresponder à versão de 32 bits, e a entrada em
``arch/x86/entry/syscalls/syscall_64.tbl`` é dividida para que os programas x32
atinjam o wrapper de compatibilidade::

    333   64            sys_xyzzy
    ...
    555   x32           __x32_compat_sys_xyzzy

Se não houver ponteiros envolvidos, então é preferível reutilizar a chamada de
sistema de 64 bits para a ABI x32 (e, consequentemente, a entrada em
``arch/x86/entry/syscalls/syscall_64.tbl`` permanece inalterada).

Em qualquer um dos casos, você deve verificar se os tipos envolvidos no layout
dos seus argumentos de fato se mapeiam exatamente do x32 (-mx32) para os seus
equivalentes de 32 bits (-m32) ou 64 bits (-m64).


Chamadas de Sistema com Retorno para Outro Local
------------------------------------------------

Para a maioria das chamadas de sistema (syscalls), assim que a execução é
concluída, o programa do usuário continua exatamente de onde parou -- na
próxima instrução, com a pilha idêntica e a maior parte dos registradores no
mesmo estado de antes da chamada, além do mesmo espaço de memória virtual.

No entanto, algumas poucas chamadas de sistema agem de forma diferente. Elas
podem retornar para um local distinto (``rt_sigreturn``), alterar o espaço de
memória (``fork``/``vfork``/``clone``) ou até mesmo modificar a arquitetura
(``execve``/``execveat``) do programa.

Para permitir isso, a implementação da chamada de sistema no kernel pode
precisar salvar e restaurar registradores adicionais na pilha do kernel,
garantindo controle total de onde e como a execução continuará após a syscall.

Isso é específico de cada arquitetura (arch-specific), mas tipicamente envolve
a definição de pontos de entrada em assembly que salvam/restauram esses
registradores adicionais e invocam o ponto de entrada real da chamada de
sistema.

Para x86_64, isso é implementado como um ponto de entrada ``stub_xyzzy`` em
``arch/x86/entry/entry_64.S``, e a entrada correspondente na tabela de syscalls
(``arch/x86/entry/syscalls/syscall_64.tbl``) é ajustada para refletir::

    333   common        stub_xyzzy

O equivalente para programas de 32 bits executados em um kernel de 64 bits é
normalmente chamado de ``stub32_xyzzy`` e implementado em
``arch/x86/entry/entry_64_compat.S``, com o respectivo ajuste na tabela de
syscalls em ``arch/x86/entry/syscalls/syscall_32.tbl``::

    380   i386          sys_xyzzy    stub32_xyzzy

Se a chamada de sistema precisar de uma camada de compatibilidade (como na
seção anterior), a versão ``stub32_`` precisará chamar a versão
``compat_sys_`` da chamada de sistema em vez da versão nativa de 64 bits. Além
disso, se a implementação da ABI x32 não for compartilhada com a versão
x86_64, sua tabela de syscalls também precisará invocar um stub que direcione
para a versão ``compat_sys_``.

Por questões de integridade, também é recomendado configurar um mapeamento para
que o User-Mode Linux (UML) continue funcionando -- sua tabela de syscalls fará
referência a ``stub_xyzzy``, mas o build do UML não inclui a implementação de
``arch/x86/entry/entry_64.S`` (já que o UML simula registradores, etc.). Corrigir
isso é tão simples quanto adicionar um #define em
``arch/x86/um/sys_call_table_64.c``::

    #define stub_xyzzy sys_xyzzy


Outros Detalhes
---------------

A maior parte do kernel trata as chamadas de sistema de maneira genérica, mas
há exceções ocasionais que podem precisar de atualização para a sua chamada
de sistema específica.

O subsistema de auditoria (audit) é um desses casos especiais; ele inclui
funções (específicas de cada arquitetura) que classificam alguns tipos
especiais de chamada de sistema -- especificamente operações de abertura de
arquivo (``open``/``openat``), execução de programa (``execve``/``exeveat``) ou
multiplexador de socket (``socketcall``). Se a sua nova chamada de sistema for
análoga a uma dessas, o sistema de auditoria deverá ser atualizado.

De forma mais geral, se existir uma chamada de sistema atual que seja análoga
à sua nova chamada de sistema, vale a pena fazer um grep em todo o kernel pela
chamada existente para verificar se não há outros casos especiais.


Testes
------

Uma nova chamada de sistema deve, obviamente, ser testada; também é útil
fornecer aos revisores uma demonstração de como os programas do espaço do
usuário (user space) usarão a chamada de sistema. Uma boa maneira de combinar
esses objetivos é incluir um programa simples de autoteste em um novo diretório
sob ``tools/testing/selftests/``.

Para uma nova chamada de sistema, obviamente não haverá uma função de wrapper
na libc e, portanto, o teste precisará invocá-la usando ``syscall()``; além
disso, se a chamada de sistema envolver uma nova estrutura visível para o
espaço do usuário, o cabeçalho correspondente precisará ser instalado para
compilar o teste.

Certifique-se de que o autoteste seja executado com sucesso em todas as
arquiteturas suportadas. Por exemplo, verifique se ele funciona quando compitado
como um programa ABI x86_64 (-m64), x86_32 (-m32) e x32 (-mx32).

Para testes mais extensos e minuciosos de novas funcionalidades, você também
deve considerar a adição de testes ao Linux Test Project ou ao projeto
xfstests para alterações relacionadas

Página de Manual (Man Page)
---------------------------

Todas as novas chamadas de sistema devem vir acompanhadas de uma página de
manual completa, idealmente usando a marcação groff, mas texto simples também
é aceitável. Se o groff for utilizado, é útil incluir uma versão ASCII pré-
renderizada da página de manual no e-mail de apresentação (cover letter) do
conjunto de patches (patchset), para a conveniência dos revisores.

A página de manual deve ser enviada com cópia (cc) para
linux-man@vger.kernel.org. Para mais detalhes, consulte
https://www.kernel.org/doc/man-pages/patches.html


Não invoque Chamadas de Sistema dentro do Kernel
------------------------------------------------

As chamadas de sistema são, como mencionado acima, pontos de interação entre o
espaço do usuário (userspace) e o kernel. Portanto, funções de chamada de
sistema como ``sys_xyzzy()`` ou ``compat_sys_xyzzy()`` só devem ser chamadas a
partir do espaço do usuário por meio da tabela de syscalls, e não de outros
lugares do kernel. Se a funcionalidade da syscall for útil para ser utilizada
dentro do kernel, precisar ser compartilhada entre uma syscall antiga e uma
nova, ou precisar ser compartilhada entre uma syscall e sua variante de
compatibilidade, ela deve ser implementada por meio de uma função auxiliadora
("helper", como ``ksys_xyzzy()``). Essa função do kernel poderá então ser
chamada dentro do stub da syscall (``sys_xyzzy()``), do stub da syscall de
compatibilidade (``compat_sys_xyzzy()``) e/ou de outro código do kernel.

Pelo menos em x86 de 64 bits, será um requisito rígido a partir da versão v4.17
em diante não chamar funções de chamadas de sistema no kernel. Essa arquitetura
utiliza uma convenção de chamada diferente para chamadas de sistema na qual a
``struct pt_regs`` é decodificada dinamicamente em um wrapper de syscall, que
então repassa o processamento para a função real da syscall. Isso significa que
apenas os parâmetros realmente necessários para uma syscall específica são
passados durante a entrada da syscall, em vez de preencher seis registradores da
CPU com conteúdos aleatórios do espaço do usuário o tempo todo (o que poderia
causar problemas sérios no decorrer da cadeia de chamadas).

Além disso, as regras sobre como os dados podem ser acessados diferem entre os
dados do kernel e os dados do usuário. Essa é outra razão pela qual chamar
``sys_xyzzy()`` geralmente é uma má ideia.

Exceções a essa regra são permitidas apenas em substituições (overrides)
específicas de cada arquitetura, wrappers de compatibilidade específicos de cada
arquitetura ou outros códigos dentro do diretório arch/.

Referências e Fontes
--------------------

 - Artigo da LWN por Michael Kerrisk sobre o uso do argumento flags em chamadas
   de sistema:
   https://lwn.net/Articles/585415/
 - Artigo da LWN por Michael Kerrisk sobre como lidar com flags desconhecidas
   em uma chamada de sistema: https://lwn.net/Articles/588444/
 - Artigo da LWN por Jake Edge descrevendo restrições em argumentos de chamadas
   de sistema de 64 bits: https://lwn.net/Articles/311630/
 - Par de artigos da LWN por David Drysdale que descrevem detalhadamente os
   caminhos de implementação de chamadas de sistema para a v3.14:

    - https://lwn.net/Articles/604287/
    - https://lwn.net/Articles/604515/

 - Os requisitos específicos de arquitetura para chamadas de sistema são
   discutidos na página de manual :manpage:`syscall(2)`:
   http://man7.org/linux/man-pages/man2/syscall.2.html#NOTES
 - E-mails compilados de Linus Torvalds discutindo os problemas com ``ioctl()``:
   https://yarchive.net/comp/linux/ioctl.html
 - "How to not invent kernel interfaces", Arnd Bergmann,
   https://www.ukuug.org/events/linux2007/2007/papers/Bergmann.pdf
 - Artigo da LWN por Michael Kerrisk sobre evitar novos usos de CAP_SYS_ADMIN:
   https://lwn.net/Articles/486306/
 - Recomendação de Andrew Morton para que todas as informações relacionadas a
   uma nova chamada de sistema venham na mesma thread de e-mail:
   https://lore.kernel.org/r/20140724144747.3041b208832bbdf9fbce5d96@linux-foundation.org
 - Recomendação de Michael Kerrisk para que uma nova chamada de sistema venha
   acompanhada de uma página de manual:
   https://lore.kernel.org/r/CAKgNAkgMA39AfoSoA5Pe1r9N+ZzfYQNvNPvcRN7tOvRb8+v06Q@mail.gmail.com
 - Sugestão de Thomas Gleixner para que a vinculação (wire-up) do x86 esteja em
   um commit separado:
   https://lore.kernel.org/r/alpine.DEB.2.11.1411191249560.3909@nanos
 - Sugestão de Greg Kroah-Hartman de que é bom que novas chamadas de sistema
   venham acompanhadas de uma página de manual e um autoteste:
   https://lore.kernel.org/r/20140320025530.GA25469@kroah.com
 - Discussão de Michael Kerrisk sobre uma nova chamada de sistema versus a
   extensão de :manpage:`prctl(2)`:
   https://lore.kernel.org/r/CAHO5Pa3F2MjfTtfNxa8LbnkeeU8=YJ+9tDqxZpw7Gz59E-4AUg@mail.gmail.com
 - Sugestão de Ingo Molnar de que as chamadas de sistema que envolvem múltiplos
   argumentos devem encapsular esses argumentos em uma struct, a qual inclua um
   campo de tamanho (size) para fins de extensibilidade futura:
   https://lore.kernel.org/r/20150730083831.GA22182@gmail.com
 - Excentricidades de numeração decorrentes do uso (e reuso) de flags do espaço
   de numeração O_*:

    - commit 75069f2b5bfb ("vfs: renumber FMODE_NONOTIFY and add to uniqueness
      check")
    - commit 12ed2e36c98a ("fanotify: FMODE_NONOTIFY and __O_SYNC in sparc
      conflict")
    - commit bb458c644a59 ("Safer ABI for O_TMPFILE")

 - Discussão de Matthew Wilcox sobre restrições em argumentos de 64 bits:
   https://lore.kernel.org/r/20081212152929.GM26095@parisc-linux.org
 - Recomendação de Greg Kroah-Hartman de que flags desconhecidas devem ser
   fiscalizadas/policiadas:
   https://lore.kernel.org/r/20140717193330.GB4703@kroah.com
 - Recomendação de Linus Torvalds de que as chamadas de sistema x32 devem
   preferir a compatibilidade com as versões de 64 bits em vez das versões de
   32 bits:
   https://lore.kernel.org/r/CA+55aFxfmwfB7jbbrXxa=K7VBYPfAvmu3XOkGrLbB1UFjX1+Ew@mail.gmail.com
 - Série de patches revisando a infraestrutura da tabela de chamadas de sistema
   para utilizar scripts/syscall.tbl em múltiplas arquiteturas:
   https://lore.kernel.org/lkml/20240704143611.2979589-1-arnd@kernel.org
