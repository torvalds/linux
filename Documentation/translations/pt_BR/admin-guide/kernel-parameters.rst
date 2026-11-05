.. SPDX-License-Identifier: GPL-2.0

Os parâmetros de linha de comando do kernel
===========================================

A seguir está uma lista consolidada dos parâmetros do kernel conforme
implementados pelas macros __setup(), early_param(), core_param() e
module_param(), ordenados na ordem do dicionário inglês (definida como ignorar
toda a pontuação e ordenar dígitos antes de letras sem distinção entre
maiúsculas e minúsculas), e com descrições onde conhecidas.

O kernel analisa os parâmetros da linha de comando do kernel até "``--``";
se ele não reconhecer um parâmetro e este não contiver um '.', o parâmetro
será passado para o init: parâmetros com '=' vão para o ambiente do init,
outros são passados como argumentos de linha de comando para o init.
Tudo após "``--``" é passado como argumento para o init.

Os parâmetros de módulo podem ser especificados de duas maneiras: através da
linha de comando do kernel com um prefixo de nome de módulo, ou via modprobe,
por exemplo::

	(linha de comando do kernel) usbcore.blinkenlights=1
	(linha de comando do modprobe) modprobe usbcore blinkenlights=1

Os parâmetros para módulos compilados embutidos no kernel precisam ser
especificados na linha de comando do kernel. O modprobe examina a linha de
comando do kernel (/proc/cmdline) e coleta parâmetros de módulo ao carregar um
módulo, de modo que a linha de comando do kernel também pode ser usada para
módulos carregáveis.

Este documento pode não estar totalmente atualizado e abrangente. O comando
"modinfo -p ${nomedomodulo}" exibe uma lista atual de todos os parâmetros de um
módulo carregável. Módulos carregáveis, após serem carregados no kernel em
execução, também revelam seus parâmetros em /sys/module/${nomedomodulo}/parameters/.
Alguns desses parâmetros podem ser alterados em tempo de execução pelo comando
``echo -n ${valor} > /sys/module/${nomedomodulo}/parameters/${param}``.

Tratamento especial
-------------------

Hífens (traços) e sublinhados são equivalentes em nomes de parâmetros, portanto::

	log_buf_len=1M print-fatal-signals=1

também pode ser inserido como::

	log-buf-len=1M print_fatal_signals=1

Aspas duplas podem ser usadas para proteger espaços nos valores, por exemplo::

	param="spaces in here"

Listas de CPUs
~~~~~~~~~~~~~~

Alguns parâmetros do kernel recebem uma lista de CPUs como valor, por exemplo:
isolcpus, nohz_full, irqaffinity, rcu_nocbs. O formato desta lista é:

	<número da cpu>,...,<número da cpu>

ou

	<número da cpu>-<número da cpu>
	(deve ser um intervalo positivo em ordem crescente)

ou uma mistura

	<número da cpu>,...,<número da cpu>-<número da cpu>

Observe que, para o caso especial de um intervalo, é possível dividir o intervalo em
grupos de tamanhos iguais e, para cada grupo, usar uma determinada quantidade a
partir do início desse grupo:

	<número da cpu>-<número da cpu>:<tamanho usado>/<tamanho do grupo>

Por exemplo, pode-se adicionar à linha de comando o seguinte parâmetro:

	isolcpus=1,2,10-20,100-2000:2/25

onde o item final representa as CPUs 100,101,125,126,150,151,...

O valor "N" pode ser usado para representar a última CPU numericamente no sistema,
ou seja, "foo_cpus=16-N" seria equivalente a "16-31" em um sistema de 32 núcleos.

Tenha em mente que "N" é dinâmico; portanto, se alterações no sistema fizerem com
que a largura do bitmap mude, como menos núcleos na lista de CPUs, N e quaisquer
intervalos que usem N também mudarão. Use o mesmo em um sistema pequeno de 4 núcleos,
e "16-N" se tornará "16-3", e agora a mesma entrada de boot será sinalizada como
inválida (início > fim).

O nome de grupo especial tolerante a maiúsculas/minúsculas "all" tem o significado
de selecionar todas as CPUs, de modo que "nohz_full=all" seja o equivalente a
"nohz_full=0-N".

A semântica de "N" e "all" é suportada em nível de bitmaps e é válida para todos
os usuários de bitmap_parselist().

Sufixos métricos
~~~~~~~~~~~~~~~~

O sufixo [KMG] é comumente descrito após vários valores de parâmetros do kernel.
Os sufixos 'K', 'M', 'G', 'T', 'P' e 'E' são permitidos. Essas letras representam
os multiplicadores _binários_ 'Quilo', 'Mega', 'Giga', 'Tera', 'Peta' e 'Exa',
equivalendo a 2^10, 2^20, 2^30, 2^40, 2^50 e 2^60 bytes, respectivamente. Esses
sufixos de letras também podem ser totalmente omitidos.

Opções de compilação do kernel
------------------------------

Os parâmetros listados abaixo só são válidos se certas opções de compilação do
kernel foram ativadas e se o respectivo hardware estiver presente. Esta lista deve
ser mantida em ordem alfabética. O texto entre colchetes no início de cada descrição
informa as restrições sob as quais um parâmetro é aplicável.

Os parâmetros indicados com BOOT são, na verdade, interpretados pelo carregador
de inicialização e não têm significado diretamente para o kernel.
Não modifique a sintaxe dos parâmetros do carregador de inicialização sem extrema
necessidade ou coordenação com <Documentation/arch/x86/boot.rst>.

Existem também parâmetros do kernel específicos de arquitetura não documentados aqui.

Observe que TODOS os parâmetros do kernel listados abaixo DISTINGUEM MAIÚSCULAS DE
MINÚSCULAS, e que um = final no nome de qualquer parâmetro indica que o parâmetro
será inserido como uma variável de ambiente, enquanto sua ausência indica que ele
aparecerá como um argumento do kernel legível via /proc/cmdline por programas em
execução assim que o sistema estiver ativo.

O número de parâmetros do kernel não é limitado, mas o comprimento da linha de
comando completa (parâmetros incluindo espaços, etc.) é limitado a um número fixo
de caracteres. Este limite depende da arquitetura e varia entre 256 e 4096 caracteres.
Ele é definido no arquivo ./include/uapi/asm-generic/setup.h como COMMAND_LINE_SIZE.

.. include:: ../../../admin-guide/kernel-parameters.txt
   :literal:
