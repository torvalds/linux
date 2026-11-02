.. SPDX-License-Identifier: GPL-2.0

.. _pt_BR_admin_devices:

Dispositivos alocados no Linux (versão 4.x+)
============================================

Esta lista é a Linux Device List, o registro oficial dos números de
dispositivo alocados e dos nós do diretório ``/dev`` para o sistema
operacional Linux.

A versão deste documento em lanana.org não é mais mantida. Esta versão, no
kernel Linux mainline, é o documento mestre. Atualizações devem ser enviadas
como patches aos mantenedores do kernel (veja o documento
:ref:`Documentation/translations/pt_BR/process/submitting-patches.rst <pt_BR_submittingpatches>`).
Explore especificamente as seções intituladas "CHAR and MISC DRIVERS" e
"BLOCK LAYER" no arquivo MAINTAINERS para encontrar os mantenedores certos
a envolver para dispositivos de caractere e de bloco.

Este documento é incluído por referência no Filesystem Hierarchy Standard
(FHS). O FHS está disponível em https://www.pathname.com/fhs/.

Alocações marcadas com (68k/Amiga) aplicam-se somente ao Linux/68k na
plataforma Amiga. Alocações marcadas com (68k/Atari) aplicam-se somente ao
Linux/68k na plataforma Atari.

Este documento está em domínio público. Os autores solicitam, no entanto,
que versões semanticamente alteradas não sejam distribuídas sem permissão
dos autores, presumindo-se que os autores possam ser contatados sem um
esforço desarrazoado.


.. attention::

  AUTORES DE DRIVERS DE DISPOSITIVO, POR FAVOR LEIAM ISTO

  O Linux agora possui amplo suporte à alocação dinâmica de numeração de
  dispositivos e pode usar ``sysfs`` e ``udev`` (``systemd``) para lidar com
  as necessidades de nomenclatura. Ainda existem algumas exceções na área de
  dispositivos seriais e de inicialização. Antes de pedir um número de
  dispositivo, certifique-se de que você realmente precisa de um.

  Para ter um número maior (major) alocado, ou um número menor (minor) nas
  situações em que isso se aplica (por exemplo, busmice), por favor envie um
  patch aos autores conforme indicado acima.

  Mantenha a descrição do dispositivo *no mesmo formato desta lista*. A razão
  para isso é que essa é a única maneira que encontramos de garantir que
  temos todas as informações necessárias para publicar o seu dispositivo e
  evitar conflitos.

  Por fim, às vezes temos que bancar a "polícia do espaço de nomes". Por
  favor, não se ofenda. Frequentemente recebemos submissões de nomes em
  ``/dev`` que fatalmente causariam conflitos no futuro. Estamos tentando
  evitar chegar a uma situação em que teríamos que sofrer uma mudança
  incompatível para a frente. Portanto, por favor, consulte-nos **antes** de
  tornar públicos, de qualquer forma, os nomes e números do seu dispositivo
  --- pelo menos até o ponto em que seria minimamente difícil alterá-los.

  Sua cooperação é apreciada.

.. include:: ../../../admin-guide/devices.txt
   :literal:

Entradas adicionais do diretório ``/dev/``
------------------------------------------

Esta seção detalha as entradas adicionais que devem ou podem existir no
diretório /dev. É preferível que os links simbólicos usem a mesma forma
(absoluta ou relativa) indicada aqui. Os links são classificados como
"físicos" (hard) ou "simbólicos", dependendo do tipo de link preferido; se
possível, o tipo de link indicado deve ser usado.

Links obrigatórios
++++++++++++++++++

Estes links devem existir em todos os sistemas:

=============== =============== =============== ==============================
/dev/fd         /proc/self/fd   simbólico       Descritores de arquivo
/dev/stdin      fd/0            simbólico       Descritor de arquivo da stdin
/dev/stdout     fd/1            simbólico       Descritor de arquivo da stdout
/dev/stderr     fd/2            simbólico       Descritor de arquivo da stderr
/dev/nfsd       socksys         simbólico       Exigido pelo iBCS-2
/dev/X0R        null            simbólico       Exigido pelo iBCS-2
=============== =============== =============== ==============================

Observação: ``/dev/X0R`` é <letra X>-<dígito 0>-<letra R>.

Links recomendados
++++++++++++++++++

Recomenda-se que estes links existam em todos os sistemas:


=============== =============== =============== ==========================
/dev/core       /proc/kcore     simbólico       Compatibilidade retroativa
/dev/ramdisk    ram0            simbólico       Compatibilidade retroativa
/dev/ftape      qft0            simbólico       Compatibilidade retroativa
/dev/bttv0      video0          simbólico       Compatibilidade retroativa
/dev/radio      radio0          simbólico       Compatibilidade retroativa
/dev/i2o*       /dev/i2o/*      simbólico       Compatibilidade retroativa
=============== =============== =============== ==========================

Os nomes alternativos ``/dev/scd?``, sugeridos anteriormente para os
``/dev/sr?`` de CD-ROM e outras unidades ópticas (que usam comandos SCSI),
foram removidos na versão 174 do ``udev``, lançada em 2011.

Links definidos localmente
++++++++++++++++++++++++++

Os links a seguir podem ser estabelecidos localmente para se adequar à
configuração do sistema. Isto é meramente uma tabulação da prática
existente, e não constitui uma recomendação. No entanto, se existirem, eles
devem ter os seguintes usos.

=============== ================= =============== ==============================
/dev/mouse      porta de mouse    simbólico       Dispositivo de mouse atual
/dev/tape       unidade de fita   simbólico       Dispositivo de fita atual
/dev/cdrom      unidade de CD-ROM simbólico       Dispositivo de CD-ROM atual
/dev/scanner    scanner           simbólico       Scanner atual
/dev/modem      porta de modem    simbólico       Dispositivo de discagem atual
/dev/root       dispositivo raiz  simbólico       Sistema de arquivos raiz atual
/dev/swap       área de swap      simbólico       Dispositivo de swap atual
=============== ================= =============== ==============================

O ``/dev/modem`` não deve ser usado para um modem que suporte tanto discagem
de entrada quanto de saída, pois isso tende a causar problemas com arquivos
de trava (lock). Se existir, o ``/dev/modem`` deve apontar para o
dispositivo TTY primário apropriado (o uso dos dispositivos alternativos de
saída é obsoleto).

Para dispositivos SCSI, o ``/dev/tape`` e o ``/dev/cdrom`` devem apontar
para os dispositivos *cozidos* (*cooked*) (``/dev/st*`` e ``/dev/sr*``,
respectivamente), enquanto o ``/dev/scanner`` deve apontar para o
dispositivo SCSI genérico apropriado (``/dev/sg*``).

O ``/dev/mouse`` pode apontar para um dispositivo TTY serial primário, um
dispositivo de mouse em hardware, ou um socket para um programa de driver de
mouse (por exemplo, ``/dev/gpmdata``).

Sockets e pipes
+++++++++++++++

Sockets e pipes nomeados não transitórios podem existir em /dev. As entradas
comuns são:

=============== ======== =============================
/dev/printer    socket   socket local do lpd
/dev/log        socket   socket local do syslog
/dev/gpmdata    socket   multiplexador de mouse do gpm
=============== ======== =============================

Pontos de montagem
++++++++++++++++++

Os nomes a seguir são reservados para a montagem de sistemas de arquivos
especiais sob /dev. Esses sistemas de arquivos especiais fornecem interfaces
do kernel que não podem ser fornecidas com nós de dispositivo padrão.

=============== ======== ==================================================
/dev/pts        devpts   Sistema de arquivos de escravos PTY
/dev/shm        tmpfs    Acesso de manutenção à memória compartilhada POSIX
=============== ======== ==================================================

Dispositivos de terminal
------------------------

Dispositivos de terminal, ou TTY, são uma classe especial de dispositivos de
caractere. Um dispositivo de terminal é qualquer dispositivo que possa atuar
como terminal de controle de uma sessão; isso inclui consoles virtuais,
portas seriais e pseudoterminais (PTYs).

Todos os dispositivos de terminal compartilham um conjunto comum de
capacidades conhecidas como disciplinas de linha (line disciplines); estas
incluem a disciplina de linha de terminal comum, bem como os modos SLIP e
PPP.

Todos os dispositivos de terminal são nomeados de forma semelhante; esta
seção explica a nomenclatura e o uso dos vários tipos de TTY. Observe que as
convenções de nomenclatura incluem várias verrugas históricas; algumas delas
são específicas do Linux, algumas foram herdadas de outros sistemas, e
algumas refletem o Linux tendo superado uma convenção emprestada.

Um sinal de cerquilha (``#``) em um nome de dispositivo é usado aqui para
indicar um número decimal sem zeros à esquerda.

Consoles virtuais e o dispositivo de console
++++++++++++++++++++++++++++++++++++++++++++

Consoles virtuais são telas de terminal em tela cheia no monitor de vídeo do
sistema. Consoles virtuais são nomeados ``/dev/tty#``, com a numeração
começando em ``/dev/tty1``; o ``/dev/tty0`` é o console virtual atual.
O ``/dev/tty0`` é o dispositivo que deve ser usado para acessar a placa de
vídeo do sistema naquelas arquiteturas para as quais os dispositivos de
frame buffer (``/dev/fb*``) não são aplicáveis. Não use o ``/dev/console``
para esse fim.

O dispositivo de console, ``/dev/console``, é o dispositivo para o qual as
mensagens do sistema devem ser enviadas, e no qual os logins devem ser
permitidos em modo monousuário. A partir do Linux 2.1.71, o ``/dev/console``
é gerenciado pelo kernel; para versões anteriores, ele deve ser um link
simbólico para o ``/dev/tty0``, para um console virtual específico como o
``/dev/tty1``, ou para um dispositivo primário de porta serial (``tty*``,
não ``cu*``), dependendo da configuração do sistema.

Portas seriais
++++++++++++++

Portas seriais são portas seriais RS-232 e qualquer dispositivo que simule
uma, seja em hardware (como modems internos) ou em software (como o driver
ISDN). No Linux, cada porta serial possui dois nomes de dispositivo: o
primário, ou de chamada de entrada (callin), e o alternativo, ou de chamada
de saída (callout). Cada tipo de dispositivo é indicado por uma letra
diferente. Para qualquer letra X, os nomes dos dispositivos são
``/dev/ttyX#`` e ``/dev/cux#``, respectivamente; por razões históricas, o
``/dev/ttyS#`` e o ``/dev/ttyC#`` correspondem ao ``/dev/cua#`` e ao
``/dev/cub#``. No futuro, deve-se esperar que múltiplas letras sejam usadas;
todas as letras serão maiúsculas para o dispositivo "tty" (por exemplo,
``/dev/ttyDP#``) e minúsculas para o dispositivo "cu" (por exemplo,
``/dev/cudp#``).

Os nomes ``/dev/ttyQ#`` e ``/dev/cuq#`` são reservados para uso local.

Os dispositivos alternativos provêem exclusão baseada no kernel e padrões um
tanto diferentes dos dispositivos primários. Seu principal propósito é
permitir o uso de portas seriais com programas sem suporte inerente a portas
seriais, ou com suporte defeituoso. Seu uso é obsoleto, e eles podem ser
removidos em uma versão futura do Linux.

A arbitragem de portas seriais é provida pelo uso de arquivos de trava com
os nomes ``/var/lock/LCK..ttyX#``. O conteúdo do arquivo de trava deve ser o
PID do processo que está travando, como um número ASCII.

É prática comum instalar links como /dev/modem, que apontam para portas
seriais. Para garantir o travamento adequado na presença desses links,
recomenda-se que o software persiga os links simbólicos e trave todos os
nomes possíveis; adicionalmente, recomenda-se que um arquivo de trava seja
instalado com o dispositivo alternativo correspondente. Para evitar
impasses (deadlocks), recomenda-se que as travas sejam adquiridas na
seguinte ordem, e liberadas na ordem inversa:

        1. O nome do link simbólico, se houver (``/var/lock/LCK..modem``)
        2. O nome "tty" (``/var/lock/LCK..ttyS2``)
        3. O nome do dispositivo alternativo (``/var/lock/LCK..cua2``)

No caso de links simbólicos aninhados, os arquivos de trava devem ser
instalados na ordem em que os links simbólicos são resolvidos.

Sob nenhuma circunstância uma aplicação deve manter uma trava enquanto
espera que outra seja liberada. Além disso, aplicações que tentam criar
arquivos de trava para os nomes de dispositivo alternativos correspondentes
devem levar em conta a possibilidade de serem usadas em um TTY que não seja
de porta serial, para o qual nenhum dispositivo alternativo existiria.

Pseudoterminais (PTYs)
++++++++++++++++++++++

Pseudoterminais, ou PTYs, são usados para criar sessões de login ou para
prover outras capacidades que exijam uma disciplina de linha TTY (incluindo
capacidade de SLIP ou PPP) a processos arbitrários geradores de dados. Cada
PTY tem um lado mestre, nomeado ``/dev/pty[p-za-e][0-9a-f]``, e um lado
escravo, nomeado ``/dev/tty[p-za-e][0-9a-f]``. O kernel arbitra o uso dos
PTYs permitindo que cada lado mestre seja aberto apenas uma vez.

Uma vez que o lado mestre tenha sido aberto, o dispositivo escravo
correspondente pode ser usado da mesma maneira que qualquer dispositivo TTY.
Os dispositivos mestre e escravo são conectados pelo kernel, gerando o
equivalente a um pipe bidirecional com capacidades de TTY.

Versões recentes dos kernels Linux e da GNU libc contêm suporte ao esquema
de nomenclatura System V/Unix98 para PTYs, que atribui um dispositivo comum,
``/dev/ptmx``, a todos os mestres (abri-lo lhe dará automaticamente um PTY
não atribuído anteriormente) e um subdiretório, ``/dev/pts``, para os
escravos; os escravos são nomeados com inteiros decimais (``/dev/pts/#`` em
nossa notação). Isso remove o problema de esgotar o espaço de nomes e
permite que o kernel crie automaticamente os nós de dispositivo para os
escravos sob demanda, usando o sistema de arquivos "devpts".
