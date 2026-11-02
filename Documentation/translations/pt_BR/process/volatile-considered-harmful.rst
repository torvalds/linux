.. SPDX-License-Identifier: GPL-2.0

Por que a classe de tipo "volatile" não deve ser usada
--------------------------------------------------------

Programadores C frequentemente interpretam volatile como uma indicação de
que uma variável pode ser alterada fora da thread de execução atual; como
resultado, às vezes são tentados a usá-la no código do kernel quando
estruturas de dados compartilhadas estão sendo utilizadas. Em outras
palavras, há quem trate tipos volatile como uma espécie de variável
atômica simplificada, mas não são. O uso de volatile no código do
kernel quase nunca é correto; este documento explica o porquê.

O ponto-chave a entender sobre volatile é que seu propósito é suprimir
otimizações, o que quase nunca é o que realmente se deseja fazer. No kernel,
é necessário proteger estruturas de dados compartilhadas contra acessos
concorrentes indesejados, o que é uma tarefa bastante diferente. O processo
de proteção contra concorrência indesejada também evita, de forma mais
eficiente, quase todos os problemas relacionados a otimizações.

Assim como volatile, as primitivas do kernel que tornam seguro o acesso
concorrente a dados (spinlocks, mutexes, barreiras de memória etc.) são
projetadas para evitar otimizações indesejadas. Se forem usadas
corretamente, também não haverá necessidade de usar volatile. Se
volatile ainda for necessário, quase certamente há algum bug no código.
Em código de kernel corretamente escrito, volatile só serve para deixar
as coisas mais lentas.

Considere um bloco típico de código do kernel::

    spin_lock(&the_lock);
    do_something_on(&shared_data);
    do_something_else_with(&shared_data);
    spin_unlock(&the_lock);

Se todo o código seguir as regras de bloqueio, o valor de shared_data não
poderá mudar inesperadamente enquanto the_lock estiver mantido. Qualquer
outro código que queira manipular esses dados estará aguardando o bloqueio.
As primitivas de spinlock atuam como barreiras de memória (são escritas
explicitamente para isso), o que significa que os acessos aos dados não
serão otimizados de forma a atravessar essas barreiras. Assim, o compilador
até pode achar que sabe qual será o valor de shared_data, mas a chamada a
spin_lock(), por atuar como barreira de memória, o forçará a esquecer tudo
o que sabia. Não haverá problemas de otimização nos acessos a esses dados.

Se shared_data fosse declarada volatile, o bloqueio ainda seria
necessário. No entanto, o compilador também seria impedido de otimizar o
acesso a shared_data _dentro_ da seção crítica, justamente quando sabemos
que ninguém mais pode estar manipulando esses dados. Enquanto o bloqueio
estiver mantido, shared_data não é volatile. Ao lidar com dados
compartilhados, um bloqueio adequado torna volatile desnecessário e
potencialmente prejudicial.

A classe de armazenamento volatile foi originalmente concebida para
registradores de E/S mapeados em memória. No kernel, os acessos a esses
registradores também devem ser protegidos por bloqueios, mas também não se
deseja que o compilador "otimize" esses acessos dentro de uma seção crítica.
Contudo, no kernel, os acessos à memória de E/S são sempre feitos por meio de
funções de acesso; acessar diretamente a memória de E/S via ponteiros é
desencorajado e não funciona em todas as arquiteturas. Essas funções de
acesso são implementadas de modo a impedir otimizações indesejadas e,
portanto, mais uma vez, volatile é desnecessário.

Outra situação em que se pode ser tentado a usar volatile é quando o
processador fica em espera ocupada pelo valor de uma variável. A forma
correta de realizar essa espera ocupada é::

    while (my_variable != what_i_want)
        cpu_relax();

A chamada a cpu_relax() pode reduzir o consumo de energia da CPU ou ceder
recursos a um processador lógico gêmeo hyperthreaded; ela também atua
como barreira para o compilador e, portanto, mais uma vez, volatile é
desnecessário. Naturalmente, a espera ocupada já é, por si só, uma prática
geralmente antissocial.

Ainda existem algumas situações raras em que volatile faz sentido no
kernel:

  - As funções de acesso mencionadas acima podem usar volatile em
    arquiteturas nas quais o acesso direto à memória de E/S funciona.
    Essencialmente, cada chamada a uma função de acesso torna-se uma
    pequena seção crítica por si só e garante que o acesso ocorra conforme
    esperado pelo programador.
  - Código assembly inline que modifica memória, mas não tem outros
    efeitos colaterais visíveis, corre o risco de ser removido pelo GCC.
    Adicionar a palavra-chave volatile às instruções asm impede essa
    remoção.
  - A variável jiffies é especial, pois pode ter um valor diferente a cada
    vez que é referenciada, mas pode ser lida sem qualquer bloqueio
    especial. Portanto, jiffies pode ser volatile, mas a adição de
    outras variáveis desse tipo é fortemente desencorajada. Nesse
    sentido, jiffies é considerada um problema de "legado idiota" (nas
    palavras de Linus); corrigi-la daria mais trabalho do que valeria a
    pena.
  - Ponteiros para estruturas de dados em memória coerente que possam ser
    modificadas por dispositivos de E/S podem, às vezes, ser
    legitimamente volatile. Um buffer circular usado por um adaptador
    de rede, no qual esse adaptador altera ponteiros para indicar quais
    descritores já foram processados, é um exemplo desse tipo de
    situação.

Na maior parte do código, nenhuma das justificativas acima para o uso de
volatile se aplica. Como resultado, o uso de volatile provavelmente
será considerado um bug e fará com que o código seja submetido a uma análise
mais rigorosa. Desenvolvedores que se sintam tentados a usar volatile devem
dar um passo atrás e pensar no que realmente estão tentando alcançar.

Patches para remover variáveis volatile são, em geral, bem-vindos, desde
que venham acompanhados de uma justificativa que demonstre que as questões
de concorrência foram devidamente analisadas.


Referências
===========

[1] https://lwn.net/Articles/233481/

[2] https://lwn.net/Articles/233482/

Créditos
========

Motivação original e pesquisa por Randy Dunlap

Escrito por Jonathan Corbet

Melhorias a partir de comentários de Satyam Sharma, Johannes Stezenbach,
Jesper Juhl, Heikki Orsila, H. Peter Anvin, Philipp Hahn e Stefan
Richter.
